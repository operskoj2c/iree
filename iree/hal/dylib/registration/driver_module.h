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

#ifndef IREE_HAL_DYLIB_REGISTRATION_DRIVER_MODULE_H_
#define IREE_HAL_DYLIB_REGISTRATION_DRIVER_MODULE_H_

#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// DEPRECATED: this entire driver will be removed soon.
// TODO(#3580): remove this entire driver w/ iree_hal_executable_library_t.
IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_hal_dylib_driver_module_register(iree_hal_driver_registry_t* registry);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DYLIB_REGISTRATION_DRIVER_MODULE_H_
