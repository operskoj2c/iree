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

#ifndef IREE_HAL_LOCAL_LOCAL_EXECUTABLE_H_
#define IREE_HAL_LOCAL_LOCAL_EXECUTABLE_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"
#include "iree/hal/local/executable_library.h"
#include "iree/hal/local/local_executable_layout.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct {
  const iree_hal_executable_dispatch_state_v0_t* state;
  iree_hal_vec3_t workgroup_id;
  iree_hal_vec3_t workgroup_size;
  iree_hal_vec3_t workgroup_count;
  iree_hal_executable_push_constants_ptr_t push_constants;
  const iree_hal_executable_binding_ptr_t* bindings;
  const iree_device_size_t* binding_lengths;
} iree_hal_local_executable_call_t;

typedef struct {
  iree_hal_resource_t resource;
  iree_hal_local_executable_layout_t* layout;
} iree_hal_local_executable_t;

typedef struct {
  iree_hal_executable_vtable_t base;

  iree_status_t(IREE_API_PTR* issue_call)(
      iree_hal_local_executable_t* executable, iree_host_size_t ordinal,
      const iree_hal_local_executable_call_t* call);
} iree_hal_local_executable_vtable_t;

void iree_hal_local_executable_initialize(
    const iree_hal_local_executable_vtable_t* vtable,
    iree_hal_local_executable_layout_t* layout,
    iree_hal_local_executable_t* out_base_executable);

void iree_hal_local_executable_deinitialize(
    iree_hal_local_executable_t* base_executable);

iree_hal_local_executable_t* iree_hal_local_executable_cast(
    iree_hal_executable_t* base_value);

iree_status_t iree_hal_local_executable_issue_call(
    iree_hal_local_executable_t* executable, iree_host_size_t ordinal,
    const iree_hal_local_executable_call_t* call);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_LOCAL_LOCAL_EXECUTABLE_H_
