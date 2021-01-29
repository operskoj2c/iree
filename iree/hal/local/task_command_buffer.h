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

#ifndef IREE_HAL_LOCAL_TASK_COMMAND_BUFFER_H_
#define IREE_HAL_LOCAL_TASK_COMMAND_BUFFER_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"
#include "iree/hal/local/arena.h"
#include "iree/hal/local/task_queue_state.h"
#include "iree/task/scope.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

iree_status_t iree_hal_task_command_buffer_create(
    iree_hal_device_t* device, iree_task_scope_t* scope,
    iree_hal_command_buffer_mode_t mode,
    iree_hal_command_category_t command_categories,
    iree_arena_block_pool_t* block_pool,
    iree_hal_command_buffer_t** out_command_buffer);

// Issues a recorded command buffer using the serial |queue_state|.
// |queue_state| is used to track the synchronization scope of the queue from
// prior commands such as signaled events and will be mutated as events are
// reset or new events are signaled.
//
// |retire_task| will be scheduled once all commands issued from the command
// buffer retire and can be used as a fence point.
//
// Any new tasks that are allocated as part of the issue operation (such as
// barrier tasks to handle event synchronization) will be acquired from |arena|.
// The lifetime of |arena| must be at least that of |retire_task| ensuring that
// all of the allocated commands issued have completed and their memory in the
// arena can be recycled.
//
// |pending_submission| will receive the ready list of commands and must be
// submitted to the executor (or discarded on failure) by the caller.
iree_status_t iree_hal_task_command_buffer_issue(
    iree_hal_command_buffer_t* command_buffer,
    iree_hal_task_queue_state_t* queue_state, iree_task_t* retire_task,
    iree_arena_allocator_t* arena, iree_task_submission_t* pending_submission);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_LOCAL_TASK_COMMAND_BUFFER_H_
