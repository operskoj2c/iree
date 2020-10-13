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

#ifndef IREE_BASE_THREADING_IMPL_H_
#define IREE_BASE_THREADING_IMPL_H_

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "iree/base/api.h"
#include "iree/base/synchronization.h"
#include "iree/base/threading.h"

#ifdef __cplusplus
extern "C" {
#endif

// strncpy_s shall copy the first N characters of src to dst, where N is the
// lesser of MaxCount and the length of src.
//
// We have this here patching over GNU being stubborn about supporting this.
// If we start using it other places we can move it into a helper file.
int iree_strncpy_s(char* dest, size_t destsz, const char* src, size_t count);

typedef void (*iree_thread_set_priority_fn_t)(
    iree_thread_t* thread, iree_thread_priority_class_t priority_class);

typedef struct {
  iree_thread_set_priority_fn_t set_priority_fn;
  iree_thread_priority_class_t base_priority_class;
  iree_allocator_t allocator;
  iree_slim_mutex_t mutex;
  iree_thread_priority_class_t current_priority_class;
  iree_thread_override_t* head;
} iree_thread_override_list_t;

// Initializes the override list for a thread with |base_priority_class|.
// |set_priority_fn| will be used to update the thread priority when needed.
void iree_thread_override_list_initialize(
    iree_thread_set_priority_fn_t set_priority_fn,
    iree_thread_priority_class_t base_priority_class,
    iree_allocator_t allocator, iree_thread_override_list_t* out_list);

// Deinitializes an override list; expects that all overrides have been removed.
void iree_thread_override_list_deinitialize(iree_thread_override_list_t* list);

// Adds a new override to the list and returns an allocated handle.
iree_thread_override_t* iree_thread_override_list_add(
    iree_thread_override_list_t* list, iree_thread_t* thread,
    iree_thread_priority_class_t priority_class);

// Removes an override from its parent list and deallocates it.
void iree_thread_override_remove_self(iree_thread_override_t* override);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // IREE_BASE_THREADING_IMPL_H_
