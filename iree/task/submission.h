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

#ifndef IREE_TASK_SUBMISSION_H_
#define IREE_TASK_SUBMISSION_H_

#include "iree/base/api.h"
#include "iree/task/list.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// A pending submission to a task queue made up of a DAG of tasks.
// Tasks are executed when ready in the order they were enqueued while observing
// all dependencies. This means that two tasks that have no dependencies may
// execute out of order/overlap.
//
// By keeping track of which tasks are ready for execution (ready_list) upon
// submission to a queue we avoid the need to walk the task list again and
// instead only touch the waiting tasks during construction and as they are made
// ready, avoiding needless work and cache thrashing.
//
// Waiting tasks (waiting_list) are those waiting on external dependencies such
// as file descriptor wait handles. Because we track all of these the executor
// can perform an efficient multi-wait across queues without needing to block
// (or even check) every waiting task individually.
//
// Because we only track roots of the DAG to release all tasks in a submission
// early (due to failure or shutdown) the DAG must be walked. Releasing just the
// lists will only handle the roots and leave all the rest of the tasks
// dangling.
//
// Thread-compatible; designed to be used from a single thread producing the
// submission.
typedef struct {
  // List of tasks that are ready for execution immediately. Upon submission to
  // a queue the tasks will be passed on to the executor with no delay.
  //
  // Tasks are stored in LIFO order; this allows us to quickly concat them with
  // incoming/mailbox slists that are naturally in LIFO order and that may
  // contain tasks from prior submissions. Note that we are representing a
  // ready list - meaning that all tasks are able to start simultaneously (in
  // the best case where tasks <= workers); this means that the ordering
  // requirements here are purely for performance and ease of debugging. In
  // cases where tasks >> workers we could also see some benefits from the
  // eventual FIFO order matching how the tasks were allocated.
  iree_task_list_t ready_list;

  // List of tasks that are waiting for execution on external dependencies.
  // These are root tasks that have no internal task dependencies.
  // Order is not important here; the assumption is that all waiting tasks are
  // more of a set than an ordered list and that they can all be waited on as a
  // multi-wait-any.
  iree_task_list_t waiting_list;
} iree_task_submission_t;

// Initializes a task submission.
void iree_task_submission_initialize(iree_task_submission_t* out_submission);

// Flushes the given |ready_slist| and initializes the submission with all tasks
// to the submission in LIFO order. All tasks in |ready_slist| are assumed to be
// ready for execution immediately.
void iree_task_submission_initialize_from_lifo_slist(
    iree_atomic_task_slist_t* ready_slist,
    iree_task_submission_t* out_submission);

// Resets the submission by dropping the list references.
void iree_task_submission_reset(iree_task_submission_t* submission);

// Discards all pending tasks in the submission. This is only safe to call if
// the submission has not yet been submitted to a queue for execution and should
// be used for failure cleanup during submission construction.
void iree_task_submission_discard(iree_task_submission_t* submission);

// Returns true if the submission has no tasks.
bool iree_task_submission_is_empty(iree_task_submission_t* submission);

// Enqueues |task| to the pending |submission|.
// The task will be checked to see whether it is immediately ready to execute
// and placed in an appropriate list; all dependencies must be declared prior to
// calling this method. After returning new tasks that depend on this task may
// still be defined. The submission takes ownership of the |task|.
void iree_task_submission_enqueue(iree_task_submission_t* submission,
                                  iree_task_t* task);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_TASK_SUBMISSION_H_
