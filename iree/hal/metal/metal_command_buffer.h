// Copyright 2020 Google LLC
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

#ifndef IREE_HAL_METAL_METAL_COMMAND_BUFFER_H_
#define IREE_HAL_METAL_METAL_COMMAND_BUFFER_H_

#import <Metal/Metal.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "iree/hal/metal/metal_buffer.h"

namespace iree {
namespace hal {
namespace metal {

// A command buffer implementation for Metal that directly wraps a
// MTLCommandBuffer.
//
// Objects of this class are not expected to be accessed by multiple threads.
class MetalCommandBuffer final : public CommandBuffer {
 public:
  static StatusOr<ref_ptr<CommandBuffer>> Create(
      iree_hal_command_buffer_mode_t mode,
      iree_hal_command_category_t command_categories,
      id<MTLCommandBuffer> command_buffer);
  ~MetalCommandBuffer() override;

  id<MTLCommandBuffer> handle() const { return metal_handle_; }

  Status Begin() override;
  Status End() override;

  Status ExecutionBarrier(
      iree_hal_execution_stage_t source_stage_mask,
      iree_hal_execution_stage_t target_stage_mask,
      absl::Span<const iree_hal_memory_barrier_t> memory_barriers,
      absl::Span<const iree_hal_buffer_barrier_t> buffer_barriers) override;

  Status SignalEvent(iree_hal_event_t* event,
                     iree_hal_execution_stage_t source_stage_mask) override;
  Status ResetEvent(iree_hal_event_t* event,
                    iree_hal_execution_stage_t source_stage_mask) override;
  Status WaitEvents(
      absl::Span<iree_hal_event_t*> events,
      iree_hal_execution_stage_t source_stage_mask,
      iree_hal_execution_stage_t target_stage_mask,
      absl::Span<const iree_hal_memory_barrier_t> memory_barriers,
      absl::Span<const iree_hal_buffer_barrier_t> buffer_barriers) override;

  Status FillBuffer(iree_hal_buffer_t* target_buffer,
                    iree_device_size_t target_offset, iree_device_size_t length,
                    const void* pattern, size_t pattern_length) override;
  Status DiscardBuffer(iree_hal_buffer_t* buffer) override;
  Status UpdateBuffer(const void* source_buffer,
                      iree_device_size_t source_offset,
                      iree_hal_buffer_t* target_buffer,
                      iree_device_size_t target_offset,
                      iree_device_size_t length) override;
  Status CopyBuffer(iree_hal_buffer_t* source_buffer,
                    iree_device_size_t source_offset,
                    iree_hal_buffer_t* target_buffer,
                    iree_device_size_t target_offset,
                    iree_device_size_t length) override;

  Status PushConstants(iree_hal_executable_layout_t* executable_layout,
                       size_t offset,
                       absl::Span<const uint32_t> values) override;

  Status PushDescriptorSet(
      iree_hal_executable_layout_t* executable_layout, uint32_t set,
      absl::Span<const iree_hal_descriptor_set_binding_t> bindings) override;
  Status BindDescriptorSet(
      iree_hal_executable_layout_t* executable_layout, uint32_t set,
      iree_hal_descriptor_set_t* descriptor_set,
      absl::Span<const iree_device_size_t> dynamic_offsets) override;

  Status Dispatch(iree_hal_executable_t* executable, int32_t entry_point,
                  std::array<uint32_t, 3> workgroups) override;
  Status DispatchIndirect(iree_hal_executable_t* executable,
                          int32_t entry_point,
                          iree_hal_buffer_t* workgroups_buffer,
                          iree_device_size_t workgroups_offset) override;

 private:
  // A struct containing all resources states of the current pipeline.
  struct PipelineStateObject {
    struct PushState {
      absl::InlinedVector<iree_hal_descriptor_set_binding_t, 8>
          resource_bindings;
    };
    // Map from set number to push descriptor states
    absl::flat_hash_map<int32_t, PushState> push_states;

    struct BindState {
      DescriptorSet* descriptor_set;
    };
    // Map from set number to bind descriptor states
    absl::flat_hash_map<int32_t, BindState> bind_states;

    struct ConstantState {
      absl::InlinedVector<uint32_t, 16> values;
    };
    // Map from set number to push constant states
    absl::flat_hash_map<uint32_t, ConstantState> constant_states;
  };

  MetalCommandBuffer(iree_hal_command_buffer_mode_t mode,
                     iree_hal_command_category_t command_categories,
                     id<MTLCommandBuffer> command_buffer);

  // Gets or begins an active MTLBlitCommandEncoder. This also ends all previous
  // encoded compute commands if any.
  id<MTLBlitCommandEncoder> GetOrBeginBlitEncoder();
  void EndBlitEncoder();

  // Gets or begins a new MTLComputeCommandEncoder. This also ends all previous
  // encoded blit commands if any.
  id<MTLComputeCommandEncoder> GetOrBeginComputeEncoder();
  void EndComputeEncoder();

 private:
  bool is_recording_ = false;
  id<MTLCommandBuffer> metal_handle_;

  id<MTLComputeCommandEncoder> current_compute_encoder_ = nil;
  id<MTLBlitCommandEncoder> current_blit_encoder_ = nil;

  absl::flat_hash_map<iree_hal_executable_layout_t*, PipelineStateObject>
      pipeline_state_objects_;
};

}  // namespace metal
}  // namespace hal
}  // namespace iree

#endif  // IREE_HAL_METAL_METAL_COMMAND_BUFFER_H_
