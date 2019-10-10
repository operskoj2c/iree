// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iree/hal/vulkan/pipeline_executable.h"

#include "absl/container/inlined_vector.h"
#include "iree/base/memory.h"
#include "iree/base/source_location.h"
#include "iree/base/status.h"
#include "iree/base/tracing.h"
#include "iree/hal/vulkan/status_util.h"

namespace iree {
namespace hal {
namespace vulkan {

// static
StatusOr<ref_ptr<PipelineExecutable>> PipelineExecutable::Create(
    const ref_ptr<VkDeviceHandle>& logical_device,
    VkPipelineCache pipeline_cache, VkPipelineLayout pipeline_layout,
    PipelineDescriptorSets descriptor_sets, ExecutableCachingModeBitfield mode,
    const SpirVExecutableDef& spirv_executable_def) {
  IREE_TRACE_SCOPE0("PipelineExecutable::Create");
  const auto& syms = logical_device->syms();
  if (!spirv_executable_def.entry_points() ||
      spirv_executable_def.entry_points()->size() == 0) {
    return InvalidArgumentErrorBuilder(IREE_LOC) << "No entry points defined";
  }
  if (!spirv_executable_def.code()) {
    return InvalidArgumentErrorBuilder(IREE_LOC) << "No SPIR-V code present";
  }
  const auto& code = *spirv_executable_def.code();

  // Create the shader module.
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = code.size() * sizeof(uint32_t);
  shader_module_create_info.pCode = code.data();
  VkShaderModule shader_module = VK_NULL_HANDLE;
  VK_RETURN_IF_ERROR(
      syms->vkCreateShaderModule(*logical_device, &shader_module_create_info,
                                 logical_device->allocator(), &shader_module));

  // We only need to keep this around during pipeline creation so ensure we
  // always clean it up when we exit this function.
  auto shader_module_cleanup = MakeCleanup([&logical_device, shader_module]() {
    logical_device->syms()->vkDestroyShaderModule(
        *logical_device, shader_module, logical_device->allocator());
  });

  // Create pipelines for each entry point.
  const auto& entry_points = *spirv_executable_def.entry_points();
  absl::InlinedVector<VkComputePipelineCreateInfo, 1> pipeline_create_infos;
  pipeline_create_infos.resize(entry_points.size());
  for (int entry_ordinal = 0; entry_ordinal < entry_points.size();
       ++entry_ordinal) {
    auto& pipeline_create_info = pipeline_create_infos[entry_ordinal];
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create_info.pNext = nullptr;
    pipeline_create_info.flags = 0;
    if (!AllBitsSet(mode, ExecutableCachingMode::kAllowOptimization)) {
      pipeline_create_info.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
    }
    if (entry_ordinal == 0) {
      pipeline_create_info.flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    } else {
      pipeline_create_info.flags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    }
    pipeline_create_info.layout = pipeline_layout;
    pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_create_info.basePipelineIndex = 0;
    auto& stage_create_info = pipeline_create_info.stage;
    stage_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_create_info.pNext = nullptr;
    stage_create_info.flags = 0;
    stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_create_info.module = shader_module;
    stage_create_info.pName = entry_points[entry_ordinal]->c_str();
    stage_create_info.pSpecializationInfo = nullptr;
  }
  absl::InlinedVector<VkPipeline, 1> pipelines;
  pipelines.resize(entry_points.size());

  // Some ICDs appear to leak in here, out of our control.
  // Warning: leak checks remain disabled if an error is returned.
  IREE_DISABLE_LEAK_CHECKS();
  VK_RETURN_IF_ERROR(syms->vkCreateComputePipelines(
      *logical_device, pipeline_cache, pipeline_create_infos.size(),
      pipeline_create_infos.data(), logical_device->allocator(),
      pipelines.data()));
  IREE_ENABLE_LEAK_CHECKS();

  auto executable =
      make_ref<PipelineExecutable>(CtorKey{}, logical_device, pipeline_layout,
                                   descriptor_sets, std::move(pipelines));
  executable->tag_ =
      spirv_executable_def.tag() ? spirv_executable_def.tag()->str() : "";
  return executable;
}

PipelineExecutable::PipelineExecutable(
    CtorKey ctor_key, const ref_ptr<VkDeviceHandle>& logical_device,
    VkPipelineLayout pipeline_layout, PipelineDescriptorSets descriptor_sets,
    absl::InlinedVector<VkPipeline, 1> pipelines)
    : logical_device_(add_ref(logical_device)),
      pipeline_layout_(pipeline_layout),
      descriptor_sets_(descriptor_sets),
      pipelines_(std::move(pipelines)) {}

PipelineExecutable::~PipelineExecutable() {
  IREE_TRACE_SCOPE0("PipelineExecutable::dtor");
  for (auto pipeline : pipelines_) {
    syms()->vkDestroyPipeline(*logical_device_, pipeline,
                              logical_device_->allocator());
  }
  pipelines_.clear();
}

StatusOr<VkPipeline> PipelineExecutable::GetPipelineForEntryPoint(
    int entry_ordinal) const {
  if (entry_ordinal < 0 || entry_ordinal >= pipelines_.size()) {
    return OutOfRangeErrorBuilder(IREE_LOC) << "Invalid entry point ordinal";
  }
  return pipelines_[entry_ordinal];
}

}  // namespace vulkan
}  // namespace hal
}  // namespace iree
