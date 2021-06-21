// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/task/api.h"

#include <stdbool.h>
#include <string.h>

#include "iree/base/internal/flags.h"
#include "iree/base/tracing.h"
#include "iree/task/topology.h"
#include "iree/task/topology_cpuinfo.h"

//===----------------------------------------------------------------------===//
// Executor configuration
//===----------------------------------------------------------------------===//

IREE_FLAG(
    bool, task_scheduling_defer_worker_startup, false,
    "Creates all workers suspended and waits until work is first scheduled to\n"
    "them to resume. This trades off initial blocking startup time waking the\n"
    "threads for potential latency additions later on as threads take longer\n"
    "to wake on their first use.");

IREE_FLAG(
    bool, task_scheduling_dedicated_wait_thread, false,
    "Creates a dedicated thread performing waits on root wait handles. On\n"
    "workloads with many short-duration waits this will reduce total\n"
    "latency as the waits are aggressively processed and dependent tasks are\n"
    "scheduled. It also keeps any wait-related syscalls off the worker\n"
    "threads that would otherwise need to perform the syscalls during\n"
    "coordination.");

IREE_FLAG(
    int32_t, task_worker_local_memory, 64 * 1024,
    "Specifies the bytes of per-worker local memory allocated for use by\n"
    "dispatched tiles. Tiles may use less than this but will fail to dispatch\n"
    "if they require more. Conceptually it is like a stack reservation and\n"
    "should be treated the same way: the source programs must be built to\n"
    "only use a specific maximum amount of local memory and the runtime must\n"
    "be configured to make at least that amount of local memory available.");

//===----------------------------------------------------------------------===//
// Topology configuration
//===----------------------------------------------------------------------===//

IREE_FLAG(
    string, task_topology_mode, "physical_cores",
    "Available modes:\n"
    " --task_topology_group_count=non-zero:\n"
    "   Uses whatever the specified group count is and ignores the set mode.\n"
    " 'physical_cores':\n"
    "   Creates one group per physical core in the machine up to\n"
    "   the value specified by --task_topology_max_group_count.\n"
    " 'unique_l2_cache_groups':\n"
    "   Creates one group for each unique L2 cache group across all available\n"
    "   cores up to the value specified by --task_topology_max_group_count.\n"
    "   This optimizes for temporal and spatial cache locality but may suffer\n"
    "   from oversubscription if there are other processes trying to use the\n"
    "   same cores.\n");

IREE_FLAG(
    int32_t, task_topology_group_count, 0,
    "Defines the total number of task system workers that will be created.\n"
    "Workers will be distributed across cores. Specifying 0 will use a\n"
    "heuristic defined by --task_topology_mode= to automatically select the\n"
    "worker count and distribution.");

IREE_FLAG(
    int32_t, task_topology_max_group_count, 8,
    "Sets a maximum value on the worker count that can be automatically\n"
    "detected and used when --task_topology_group_count=0 and is ignored\n"
    "otherwise.\n");

// TODO(benvanik): add --task_topology_dump to dump out the current machine
// configuration as seen by the topology utilities.

//===----------------------------------------------------------------------===//
// Task system factory functions
//===----------------------------------------------------------------------===//

iree_status_t iree_task_executor_create_from_flags(
    iree_allocator_t host_allocator, iree_task_executor_t** out_executor) {
  IREE_ASSERT_ARGUMENT(out_executor);
  *out_executor = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_task_scheduling_mode_t scheduling_mode = 0;
  if (FLAG_task_scheduling_defer_worker_startup) {
    scheduling_mode |= IREE_TASK_SCHEDULING_MODE_DEFER_WORKER_STARTUP;
  }
  if (FLAG_task_scheduling_dedicated_wait_thread) {
    scheduling_mode |= IREE_TASK_SCHEDULING_MODE_DEDICATED_WAIT_THREAD;
  }

  iree_host_size_t worker_local_memory =
      (iree_host_size_t)FLAG_task_worker_local_memory;

  iree_status_t status = iree_ok_status();

  iree_task_topology_t topology;
  iree_task_topology_initialize(&topology);

  if (FLAG_task_topology_group_count != 0) {
    iree_task_topology_initialize_from_group_count(
        FLAG_task_topology_group_count, &topology);
  } else if (strcmp(FLAG_task_topology_mode, "physical_cores") == 0) {
    iree_task_topology_initialize_from_physical_cores(
        FLAG_task_topology_max_group_count, &topology);
  } else if (strcmp(FLAG_task_topology_mode, "unique_l2_cache_groups") == 0) {
    iree_task_topology_initialize_from_unique_l2_cache_groups(
        FLAG_task_topology_max_group_count, &topology);
  } else {
    status = iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "one of --task_topology_group_count or --task_topology_mode must be "
        "specified and be a valid value; have --task_topology_mode=%s.",
        FLAG_task_topology_mode);
  }

  if (iree_status_is_ok(status)) {
    status = iree_task_executor_create(scheduling_mode, &topology,
                                       worker_local_memory, host_allocator,
                                       out_executor);
  }

  iree_task_topology_deinitialize(&topology);

  IREE_TRACE_ZONE_END(z0);
  return status;
}
