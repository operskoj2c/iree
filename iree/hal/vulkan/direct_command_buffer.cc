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

#include "iree/hal/vulkan/direct_command_buffer.h"

#include "absl/base/attributes.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/source_location.h"
#include "iree/base/status.h"
#include "iree/base/tracing.h"
#include "iree/hal/vulkan/status_util.h"

namespace iree {
namespace hal {
namespace vulkan {

namespace {

VkPipelineStageFlags ConvertPipelineStageFlags(
    ExecutionStageBitfield stage_mask) {
  VkPipelineStageFlags flags = 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kCommandIssue)
               ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
               : 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kCommandProcess)
               ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
               : 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kDispatch)
               ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
               : 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kTransfer)
               ? VK_PIPELINE_STAGE_TRANSFER_BIT
               : 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kCommandRetire)
               ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
               : 0;
  flags |= AnyBitSet(stage_mask & ExecutionStage::kHost)
               ? VK_PIPELINE_STAGE_HOST_BIT
               : 0;
  return flags;
}

VkAccessFlags ConvertAccessMask(AccessScopeBitfield access_mask) {
  VkAccessFlags flags = 0;
  flags |= AnyBitSet(access_mask & AccessScope::kIndirectCommandRead)
               ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kConstantRead)
               ? VK_ACCESS_UNIFORM_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kDispatchRead)
               ? VK_ACCESS_SHADER_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kDispatchWrite)
               ? VK_ACCESS_SHADER_WRITE_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kTransferRead)
               ? VK_ACCESS_TRANSFER_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kTransferWrite)
               ? VK_ACCESS_TRANSFER_WRITE_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kHostRead)
               ? VK_ACCESS_HOST_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kHostWrite)
               ? VK_ACCESS_HOST_WRITE_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kMemoryRead)
               ? VK_ACCESS_MEMORY_READ_BIT
               : 0;
  flags |= AnyBitSet(access_mask & AccessScope::kMemoryWrite)
               ? VK_ACCESS_MEMORY_WRITE_BIT
               : 0;
  return flags;
}

// Splats a pattern value of 1, 2, or 4 bytes out to a 4 byte value.
uint32_t SplatPattern(const void* pattern, size_t pattern_length) {
  switch (pattern_length) {
    case 1: {
      uint32_t pattern_value = *static_cast<const uint8_t*>(pattern);
      return (pattern_value << 24) | (pattern_value << 16) |
             (pattern_value << 8) | pattern_value;
    }
    case 2: {
      uint32_t pattern_value = *static_cast<const uint16_t*>(pattern);
      return (pattern_value << 16) | pattern_value;
    }
    case 4: {
      uint32_t pattern_value = *static_cast<const uint32_t*>(pattern);
      return pattern_value;
    }
    default:
      return 0;  // Already verified that this should not be possible.
  }
}

}  // namespace

DirectCommandBuffer::DirectCommandBuffer(
    Allocator* allocator, CommandBufferModeBitfield mode,
    CommandCategoryBitfield command_categories,
    const ref_ptr<VkCommandPoolHandle>& command_pool,
    VkCommandBuffer command_buffer)
    : CommandBuffer(allocator, mode, command_categories),
      command_pool_(add_ref(command_pool)),
      command_buffer_(command_buffer) {}

DirectCommandBuffer::~DirectCommandBuffer() {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::dtor");
  absl::MutexLock lock(command_pool_->mutex());
  syms()->vkFreeCommandBuffers(*command_pool_->logical_device(), *command_pool_,
                               1, &command_buffer_);
}

StatusOr<NativeEvent*> DirectCommandBuffer::CastEvent(Event* event) const {
  // TODO(benvanik): assert the event is valid.
  return static_cast<NativeEvent*>(event);
}

StatusOr<VmaBuffer*> DirectCommandBuffer::CastBuffer(Buffer* buffer) const {
  // TODO(benvanik): assert that the buffer is from the right allocator and
  // that it is compatible with our target queue family.
  return static_cast<VmaBuffer*>(buffer->allocated_buffer());
}

Status DirectCommandBuffer::Begin() {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::Begin");

  is_recording_ = true;

  VkCommandBufferBeginInfo begin_info;
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = nullptr;
  begin_info.flags = AllBitsSet(mode(), CommandBufferMode::kOneShot)
                         ? VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                         : 0;
  begin_info.pInheritanceInfo = nullptr;
  VK_RETURN_IF_ERROR(
      syms()->vkBeginCommandBuffer(command_buffer_, &begin_info));

  return OkStatus();
}

Status DirectCommandBuffer::End() {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::End");

  VK_RETURN_IF_ERROR(syms()->vkEndCommandBuffer(command_buffer_));

  is_recording_ = false;

  return OkStatus();
}

Status DirectCommandBuffer::ExecutionBarrier(
    ExecutionStageBitfield source_stage_mask,
    ExecutionStageBitfield target_stage_mask,
    absl::Span<const MemoryBarrier> memory_barriers,
    absl::Span<const BufferBarrier> buffer_barriers) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::ExecutionBarrier");

  absl::InlinedVector<VkMemoryBarrier, 8> memory_barrier_infos(
      memory_barriers.size());
  for (int i = 0; i < memory_barriers.size(); ++i) {
    const auto& memory_barrier = memory_barriers[i];
    auto& info = memory_barrier_infos[i];
    info.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    info.pNext = nullptr;
    info.srcAccessMask = ConvertAccessMask(memory_barrier.source_scope);
    info.dstAccessMask = ConvertAccessMask(memory_barrier.target_scope);
  }

  absl::InlinedVector<VkBufferMemoryBarrier, 8> buffer_barrier_infos(
      buffer_barriers.size());
  for (int i = 0; i < buffer_barriers.size(); ++i) {
    const auto& buffer_barrier = buffer_barriers[i];
    auto& info = buffer_barrier_infos[i];
    info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    info.pNext = nullptr;
    info.srcAccessMask = ConvertAccessMask(buffer_barrier.source_scope);
    info.dstAccessMask = ConvertAccessMask(buffer_barrier.target_scope);
    info.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    info.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ASSIGN_OR_RETURN(auto* device_buffer, CastBuffer(buffer_barrier.buffer));
    info.buffer = device_buffer->handle();
    info.offset = buffer_barrier.offset;
    info.size = buffer_barrier.length;
  }

  syms()->vkCmdPipelineBarrier(
      command_buffer_, ConvertPipelineStageFlags(source_stage_mask),
      ConvertPipelineStageFlags(target_stage_mask), /*dependencyFlags=*/0,
      memory_barrier_infos.size(), memory_barrier_infos.data(),
      buffer_barrier_infos.size(), buffer_barrier_infos.data(), 0, nullptr);

  return OkStatus();
}

Status DirectCommandBuffer::SignalEvent(
    Event* event, ExecutionStageBitfield source_stage_mask) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::SignalEvent");
  ASSIGN_OR_RETURN(auto* device_event, CastEvent(event));
  syms()->vkCmdSetEvent(command_buffer_, device_event->handle(),
                        ConvertPipelineStageFlags(source_stage_mask));
  return OkStatus();
}

Status DirectCommandBuffer::ResetEvent(
    Event* event, ExecutionStageBitfield source_stage_mask) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::ResetEvent");
  ASSIGN_OR_RETURN(auto* device_event, CastEvent(event));
  syms()->vkCmdResetEvent(command_buffer_, device_event->handle(),
                          ConvertPipelineStageFlags(source_stage_mask));
  return OkStatus();
}

Status DirectCommandBuffer::WaitEvents(
    absl::Span<Event*> events, ExecutionStageBitfield source_stage_mask,
    ExecutionStageBitfield target_stage_mask,
    absl::Span<const MemoryBarrier> memory_barriers,
    absl::Span<const BufferBarrier> buffer_barriers) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::WaitEvents");

  absl::InlinedVector<VkEvent, 4> event_handles(events.size());
  for (int i = 0; i < events.size(); ++i) {
    ASSIGN_OR_RETURN(auto* device_event, CastEvent(events[i]));
    event_handles[i] = device_event->handle();
  }

  absl::InlinedVector<VkMemoryBarrier, 8> memory_barrier_infos(
      memory_barriers.size());
  for (int i = 0; i < memory_barriers.size(); ++i) {
    const auto& memory_barrier = memory_barriers[i];
    auto& info = memory_barrier_infos[i];
    info.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    info.pNext = nullptr;
    info.srcAccessMask = ConvertAccessMask(memory_barrier.source_scope);
    info.dstAccessMask = ConvertAccessMask(memory_barrier.target_scope);
  }

  absl::InlinedVector<VkBufferMemoryBarrier, 8> buffer_barrier_infos(
      buffer_barriers.size());
  for (int i = 0; i < buffer_barriers.size(); ++i) {
    const auto& buffer_barrier = buffer_barriers[i];
    auto& info = buffer_barrier_infos[i];
    info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    info.pNext = nullptr;
    info.srcAccessMask = ConvertAccessMask(buffer_barrier.source_scope);
    info.dstAccessMask = ConvertAccessMask(buffer_barrier.target_scope);
    info.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    info.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ASSIGN_OR_RETURN(auto* device_buffer, CastBuffer(buffer_barrier.buffer));
    info.buffer = device_buffer->handle();
    info.offset = buffer_barrier.offset;
    info.size = buffer_barrier.length;
  }

  syms()->vkCmdWaitEvents(
      command_buffer_, event_handles.size(), event_handles.data(),
      ConvertPipelineStageFlags(source_stage_mask),
      ConvertPipelineStageFlags(target_stage_mask), memory_barrier_infos.size(),
      memory_barrier_infos.data(), buffer_barrier_infos.size(),
      buffer_barrier_infos.data(), 0, nullptr);
  return OkStatus();
}

Status DirectCommandBuffer::FillBuffer(Buffer* target_buffer,
                                       device_size_t target_offset,
                                       device_size_t length,
                                       const void* pattern,
                                       size_t pattern_length) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::FillBuffer");
  ASSIGN_OR_RETURN(auto* target_device_buffer, CastBuffer(target_buffer));

  // Note that fill only accepts 4-byte aligned values so we need to splat out
  // our variable-length pattern.
  target_offset += target_buffer->byte_offset();
  uint32_t dword_pattern = SplatPattern(pattern, pattern_length);
  syms()->vkCmdFillBuffer(command_buffer_, target_device_buffer->handle(),
                          target_offset, length, dword_pattern);

  return OkStatus();
}

Status DirectCommandBuffer::DiscardBuffer(Buffer* buffer) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::DiscardBuffer");
  // NOTE: we could use this to prevent queue family transitions.
  return OkStatus();
}

Status DirectCommandBuffer::UpdateBuffer(const void* source_buffer,
                                         device_size_t source_offset,
                                         Buffer* target_buffer,
                                         device_size_t target_offset,
                                         device_size_t length) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::UpdateBuffer");
  ASSIGN_OR_RETURN(auto* target_device_buffer, CastBuffer(target_buffer));

  // Vulkan only allows updates of <= 65536 because you really, really, really
  // shouldn't do large updates like this (as it wastes command buffer space and
  // may be slower than just using write-through mapped memory). The
  // recommendation in the spec for larger updates is to split the single update
  // into multiple updates over the entire desired range.
  const auto* source_buffer_ptr = static_cast<const uint8_t*>(source_buffer);
  target_offset += target_buffer->byte_offset();
  while (length > 0) {
    device_size_t chunk_length =
        std::min(static_cast<device_size_t>(65536u), length);
    syms()->vkCmdUpdateBuffer(command_buffer_, target_device_buffer->handle(),
                              target_offset, chunk_length, source_buffer_ptr);
    source_buffer_ptr += chunk_length;
    target_offset += chunk_length;
    length -= chunk_length;
  }

  return OkStatus();
}

Status DirectCommandBuffer::CopyBuffer(Buffer* source_buffer,
                                       device_size_t source_offset,
                                       Buffer* target_buffer,
                                       device_size_t target_offset,
                                       device_size_t length) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::CopyBuffer");
  ASSIGN_OR_RETURN(auto* source_device_buffer, CastBuffer(source_buffer));
  ASSIGN_OR_RETURN(auto* target_device_buffer, CastBuffer(target_buffer));

  VkBufferCopy region;
  region.srcOffset = source_buffer->byte_offset() + source_offset;
  region.dstOffset = target_buffer->byte_offset() + target_offset;
  region.size = length;
  syms()->vkCmdCopyBuffer(command_buffer_, source_device_buffer->handle(),
                          target_device_buffer->handle(), 1, &region);

  return OkStatus();
}

Status DirectCommandBuffer::UpdateAndBindDescriptorSet(
    PipelineExecutable* executable, absl::Span<const BufferBinding> bindings) {
  absl::InlinedVector<VkDescriptorBufferInfo, 8> buffer_infos;
  buffer_infos.resize(bindings.size());
  for (int i = 0; i < bindings.size(); ++i) {
    ASSIGN_OR_RETURN(auto buffer, CastBuffer(bindings[i].buffer));
    buffer_infos[i].buffer = buffer->handle();
    // TODO(benvanik): properly subrange (add to BufferBinding).
    buffer_infos[i].offset = bindings[i].buffer->byte_offset();
    buffer_infos[i].range = bindings[i].buffer->byte_length();
  }

  const auto& descriptor_sets = executable->descriptor_sets();
  absl::InlinedVector<VkWriteDescriptorSet, 8> write_infos;
  write_infos.resize(bindings.size());
  for (int i = 0; i < bindings.size(); ++i) {
    ASSIGN_OR_RETURN(auto buffer, CastBuffer(bindings[i].buffer));
    VkDescriptorBufferInfo buffer_info;
    buffer_info.buffer = buffer->handle();
    // TODO(benvanik): properly subrange (add to BufferBinding).
    buffer_info.offset = bindings[i].buffer->byte_offset();
    buffer_info.range = bindings[i].buffer->byte_length();
    auto& write_info = write_infos[i];
    write_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_info.pNext = nullptr;
    write_info.dstSet = VK_NULL_HANDLE;
    write_info.dstBinding = descriptor_sets.buffer_binding_set_map[i];
    write_info.dstArrayElement = 0;
    write_info.descriptorCount = 1;
    write_info.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_info.pImageInfo = nullptr;
    write_info.pBufferInfo = &buffer_infos[i];
    write_info.pTexelBufferView = nullptr;
  }

  if (command_pool_->logical_device()->enabled_extensions().push_descriptors) {
    // Fast path using push descriptors. These are pooled internally by the
    // command buffer and prevent the need for our own pooling mechanisms.
    syms()->vkCmdPushDescriptorSetKHR(
        command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
        executable->pipeline_layout(), descriptor_sets.buffer_binding_set,
        write_infos.size(), write_infos.data());
  } else {
    // TODO(benvanik): allocate from pool and update.
    return UnimplementedErrorBuilder(ABSL_LOC)
           << "Non-push descriptor set path not yet implemented";
  }

  return OkStatus();
}

Status DirectCommandBuffer::Dispatch(const DispatchRequest& dispatch_request) {
  IREE_TRACE_SCOPE0("DirectCommandBuffer::Dispatch");

  // Get the compiled and linked pipeline for the specified entry point and
  // bind it to the command buffer.
  auto* executable =
      static_cast<PipelineExecutable*>(dispatch_request.executable);
  ASSIGN_OR_RETURN(VkPipeline pipeline, executable->GetPipelineForEntryPoint(
                                            dispatch_request.entry_point));
  syms()->vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline);

  // Either allocate, update, and bind a descriptor set or use push descriptor
  // sets to use the command buffer pool when supported.
  RETURN_IF_ERROR(
      UpdateAndBindDescriptorSet(executable, dispatch_request.bindings));

  // TODO(benvanik): not this, /obviously/. Replace with semantic tags or just
  // get SPIR-V roundtripping what we need to do this in proper IR. The infra
  // for dynamic shapes is another route, with us being able to just pass shapes
  // in via dynamically updated uniform buffers.
  if (executable->is_matmul()) {
    struct ABSL_ATTRIBUTE_PACKED {
      int32_t dims[4];
    } shapes[3];
    for (int i = 0; i < 3; ++i) {
      const auto& shape = dispatch_request.bindings[i].shape;
      if (shape.size() == 3) {
        shapes[i].dims[0] = shape[0];
        shapes[i].dims[1] = shape[1];
        shapes[i].dims[2] = shape[2];
        shapes[i].dims[3] = 1;
      } else {
        shapes[i].dims[0] = 1;
        shapes[i].dims[1] = shape[0];
        shapes[i].dims[2] = shape[1];
        shapes[i].dims[3] = 1;
      }
    }
    syms()->vkCmdPushConstants(command_buffer_, executable->pipeline_layout(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shapes),
                               &shapes);
  }

  // TODO(benvanik): divide workload by caps and issue multiple dispatches.
  // TODO(benvanik): track local workgroup/subgroup size and divide into groups.
  if (dispatch_request.workload_buffer) {
    return UnimplementedErrorBuilder(ABSL_LOC)
           << "Dynamic dispatches not yet implemented";
  }
  uint32_t group_count_x = dispatch_request.workload[0];
  uint32_t group_count_y = dispatch_request.workload[1];
  uint32_t group_count_z = dispatch_request.workload[2];

  if (executable->is_matmul()) {
    group_count_x = (group_count_x + 16 - 1) / 16;
    group_count_y = (group_count_y + 16 - 1) / 16;
    group_count_z = 1;
  }

  syms()->vkCmdDispatch(command_buffer_, group_count_x, group_count_y,
                        group_count_z);

  return OkStatus();
}

}  // namespace vulkan
}  // namespace hal
}  // namespace iree
