// Copyright 2022 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/local/loaders/static_library_loader.h"
#include "iree/hal/local/task_device.h"
#include "iree/task/api.h"
#include "mnist_static.h"

iree_status_t create_device_with_static_loader(iree_allocator_t host_allocator,
                                               iree_hal_device_t** out_device) {
  iree_hal_task_device_params_t params;
  iree_hal_task_device_params_initialize(&params);

  // Load the statically embedded library.
  const iree_hal_executable_library_header_t** static_library =
      mnist_linked_llvm_library_query(
          IREE_HAL_EXECUTABLE_LIBRARY_LATEST_VERSION,
          /*reserved=*/NULL);
  const iree_hal_executable_library_header_t** libraries[1] = {static_library};

  iree_hal_executable_loader_t* library_loader = NULL;
  iree_status_t status = iree_hal_static_library_loader_create(
      IREE_ARRAYSIZE(libraries), libraries,
      iree_hal_executable_import_provider_null(), host_allocator,
      &library_loader);

  // Create a task executor.
  iree_task_executor_t* executor = NULL;
  iree_task_scheduling_mode_t scheduling_mode = 0;
  iree_host_size_t worker_local_memory = 0;
  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);
  // TODO(scotttodd): Try with more threads
  iree_task_topology_initialize_from_group_count(/*group_count=*/1, &topology);
  if (iree_status_is_ok(status)) {
    status = iree_task_executor_create(scheduling_mode, &topology,
                                       worker_local_memory, host_allocator,
                                       &executor);
  }
  iree_task_topology_deinitialize(&topology);

  iree_string_view_t identifier = iree_make_cstring_view("task");
  iree_hal_allocator_t* device_allocator = NULL;
  if (iree_status_is_ok(status)) {
    status = iree_hal_allocator_create_heap(identifier, host_allocator,
                                            host_allocator, &device_allocator);
  }

  if (iree_status_is_ok(status)) {
    status = iree_hal_task_device_create(
        identifier, &params, executor, /*loader_count=*/1, &library_loader,
        device_allocator, host_allocator, out_device);
  }

  iree_hal_allocator_release(device_allocator);
  iree_task_executor_release(executor);
  iree_hal_executable_loader_release(library_loader);
  return status;
}
