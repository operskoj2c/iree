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

#include "iree/hal/driver.h"

#include "iree/base/tracing.h"
#include "iree/hal/detail.h"

#define _VTABLE_DISPATCH(driver, method_name) \
  IREE_HAL_VTABLE_DISPATCH(driver, iree_hal_driver, method_name)

IREE_HAL_API_RETAIN_RELEASE(driver);

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_hal_driver_query_available_devices(
    iree_hal_driver_t* driver, iree_allocator_t allocator,
    iree_hal_device_info_t** out_device_infos,
    iree_host_size_t* out_device_info_count) {
  IREE_ASSERT_ARGUMENT(driver);
  IREE_ASSERT_ARGUMENT(out_device_infos);
  IREE_ASSERT_ARGUMENT(out_device_info_count);
  *out_device_info_count = 0;
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_status_t status = _VTABLE_DISPATCH(driver, query_available_devices)(
      driver, allocator, out_device_infos, out_device_info_count);
  IREE_TRACE_ZONE_END(z0);
  return status;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL iree_hal_driver_create_device(
    iree_hal_driver_t* driver, iree_hal_device_id_t device_id,
    iree_allocator_t allocator, iree_hal_device_t** out_device) {
  IREE_ASSERT_ARGUMENT(driver);
  IREE_ASSERT_ARGUMENT(out_device);
  *out_device = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_status_t status = _VTABLE_DISPATCH(driver, create_device)(
      driver, device_id, allocator, out_device);
  IREE_TRACE_ZONE_END(z0);
  return status;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_hal_driver_create_default_device(iree_hal_driver_t* driver,
                                      iree_allocator_t allocator,
                                      iree_hal_device_t** out_device) {
  IREE_ASSERT_ARGUMENT(driver);
  IREE_ASSERT_ARGUMENT(out_device);
  *out_device = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_status_t status = _VTABLE_DISPATCH(driver, create_device)(
      driver, IREE_HAL_DRIVER_ID_INVALID, allocator, out_device);
  IREE_TRACE_ZONE_END(z0);
  return status;
}
