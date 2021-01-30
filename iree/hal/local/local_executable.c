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

#include "iree/hal/local/local_executable.h"

void iree_hal_local_executable_initialize(
    const iree_hal_local_executable_vtable_t* vtable,
    iree_host_size_t executable_layout_count,
    iree_hal_executable_layout_t* const* source_executable_layouts,
    iree_hal_local_executable_layout_t** target_executable_layouts,
    iree_allocator_t host_allocator,
    iree_hal_local_executable_t* out_base_executable) {
  iree_hal_resource_initialize(vtable, &out_base_executable->resource);
  out_base_executable->host_allocator = host_allocator;

  out_base_executable->executable_layout_count = executable_layout_count;
  out_base_executable->executable_layouts = target_executable_layouts;
  for (iree_host_size_t i = 0; i < executable_layout_count; ++i) {
    target_executable_layouts[i] =
        (iree_hal_local_executable_layout_t*)source_executable_layouts[i];
    iree_hal_executable_layout_retain(source_executable_layouts[i]);
  }
}

void iree_hal_local_executable_deinitialize(
    iree_hal_local_executable_t* base_executable) {
  for (iree_host_size_t i = 0; i < base_executable->executable_layout_count;
       ++i) {
    iree_hal_executable_layout_release(
        (iree_hal_executable_layout_t*)base_executable->executable_layouts[i]);
  }
}

iree_hal_local_executable_t* iree_hal_local_executable_cast(
    iree_hal_executable_t* base_value) {
  return (iree_hal_local_executable_t*)base_value;
}

iree_status_t iree_hal_local_executable_issue_call(
    iree_hal_local_executable_t* executable, iree_host_size_t ordinal,
    const iree_hal_local_executable_call_t* call) {
  IREE_ASSERT_ARGUMENT(executable);
  IREE_ASSERT_ARGUMENT(call);
  return ((const iree_hal_local_executable_vtable_t*)
              executable->resource.vtable)
      ->issue_call(executable, ordinal, call);
}
