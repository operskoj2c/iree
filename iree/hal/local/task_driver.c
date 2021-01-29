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

#include "iree/hal/local/task_driver.h"

#include "iree/base/tracing.h"

#define IREE_HAL_TASK_DEVICE_ID_DEFAULT 0

typedef struct {
  iree_hal_resource_t resource;
  iree_allocator_t host_allocator;

  iree_string_view_t identifier;
  iree_hal_task_device_params_t default_params;

  iree_task_executor_t* executor;

  iree_host_size_t loader_count;
  iree_hal_executable_loader_t* loaders[];
} iree_hal_task_driver_t;

static const iree_hal_driver_vtable_t iree_hal_task_driver_vtable;

iree_status_t iree_hal_task_driver_create(
    iree_string_view_t identifier,
    const iree_hal_task_device_params_t* default_params,
    iree_task_executor_t* executor, iree_host_size_t loader_count,
    iree_hal_executable_loader_t** loaders, iree_allocator_t host_allocator,
    iree_hal_driver_t** out_driver) {
  IREE_ASSERT_ARGUMENT(default_params);
  IREE_ASSERT_ARGUMENT(!loader_count || loaders);
  IREE_ASSERT_ARGUMENT(out_driver);
  *out_driver = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_hal_task_driver_t* driver = NULL;
  iree_host_size_t total_size = sizeof(*driver) +
                                loader_count * sizeof(*driver->loaders) +
                                identifier.size;
  iree_status_t status =
      iree_allocator_malloc(host_allocator, total_size, (void**)&driver);
  if (iree_status_is_ok(status)) {
    iree_hal_resource_initialize(&iree_hal_task_driver_vtable,
                                 &driver->resource);
    driver->host_allocator = host_allocator;

    iree_string_view_append_to_buffer(
        identifier, &driver->identifier,
        (char*)driver + total_size - identifier.size);
    memcpy(&driver->default_params, default_params,
           sizeof(driver->default_params));

    driver->executor = executor;
    iree_task_executor_retain(driver->executor);

    driver->loader_count = loader_count;
    for (iree_host_size_t i = 0; i < driver->loader_count; ++i) {
      driver->loaders[i] = loaders[i];
      iree_hal_executable_loader_retain(driver->loaders[i]);
    }
  }

  if (iree_status_is_ok(status)) {
    *out_driver = (iree_hal_driver_t*)driver;
  } else {
    iree_hal_driver_release((iree_hal_driver_t*)driver);
  }
  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_task_driver_destroy(iree_hal_driver_t* base_driver) {
  iree_hal_task_driver_t* driver = (iree_hal_task_driver_t*)base_driver;
  iree_allocator_t host_allocator = driver->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  for (iree_host_size_t i = 0; i < driver->loader_count; ++i) {
    iree_hal_executable_loader_release(driver->loaders[i]);
  }
  iree_task_executor_release(driver->executor);
  iree_allocator_free(host_allocator, driver);

  IREE_TRACE_ZONE_END(z0);
}

static iree_status_t iree_hal_task_driver_query_available_devices(
    iree_hal_driver_t* base_driver, iree_allocator_t allocator,
    iree_hal_device_info_t** out_device_infos,
    iree_host_size_t* out_device_info_count) {
  static const iree_hal_device_info_t device_infos[1] = {
      {
          .device_id = IREE_HAL_TASK_DEVICE_ID_DEFAULT,
          .name = iree_string_view_literal("default"),
      },
  };
  *out_device_info_count = IREE_ARRAYSIZE(device_infos);
  return iree_allocator_clone(
      allocator, iree_make_const_byte_span(device_infos, sizeof(device_infos)),
      (void**)out_device_infos);
}

static iree_status_t iree_hal_task_driver_create_device(
    iree_hal_driver_t* base_driver, iree_hal_device_id_t device_id,
    iree_allocator_t allocator, iree_hal_device_t** out_device) {
  iree_hal_task_driver_t* driver = (iree_hal_task_driver_t*)base_driver;
  return iree_hal_task_device_create(
      driver->identifier, &driver->default_params, driver->executor,
      driver->loader_count, driver->loaders, allocator, out_device);
}

static const iree_hal_driver_vtable_t iree_hal_task_driver_vtable = {
    .destroy = iree_hal_task_driver_destroy,
    .query_available_devices = iree_hal_task_driver_query_available_devices,
    .create_device = iree_hal_task_driver_create_device,
};
