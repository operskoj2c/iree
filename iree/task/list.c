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

#include "iree/task/list.h"

void iree_atomic_task_slist_discard(iree_atomic_task_slist_t* slist) {
  iree_task_list_t discard_list;
  iree_task_list_initialize(&discard_list);

  // Flush the entire slist and walk it discarding the tasks as we go. This
  // avoids the need to do more than one walk if we were simply trying to flush
  // and discard into an iree_task_list_t.
  //
  // Note that we may accumulate additional tasks that need to be discarded;
  // that's when we use the iree_task_list_t discard logic which works because
  // we have a head/tail and are no longer in atomic land.
  iree_task_t* task_head = NULL;
  iree_atomic_task_slist_flush(
      slist, IREE_ATOMIC_SLIST_FLUSH_ORDER_APPROXIMATE_LIFO, &task_head, NULL);
  while (task_head != NULL) {
    iree_task_t* next_task = task_head->next_task;
    iree_task_discard(task_head, &discard_list);
    task_head = next_task;
  }

  iree_task_list_discard(&discard_list);
}

void iree_task_list_initialize(iree_task_list_t* out_list) {
  memset(out_list, 0, sizeof(*out_list));
}

void iree_task_list_move(iree_task_list_t* list, iree_task_list_t* out_list) {
  memcpy(out_list, list, sizeof(*out_list));
  memset(list, 0, sizeof(*list));
}

void iree_task_list_discard(iree_task_list_t* list) {
  // Fixed point iteration over the task list and all its transitive dependent
  // tasks that get discarded. This is in contrast to a recursive discard that
  // could potentially be thousands of calls deep in a large graph.
  while (!iree_task_list_is_empty(list)) {
    iree_task_t* task = iree_task_list_pop_front(list);
    iree_task_discard(task, list);
  }
}

bool iree_task_list_is_empty(const iree_task_list_t* list) {
  return list->head == NULL;
}

iree_host_size_t iree_task_list_calculate_size(const iree_task_list_t* list) {
  iree_host_size_t count = 0;
  iree_task_t* p = list->head;
  while (p) {
    ++count;
    p = p->next_task;
  }
  return count;
}

iree_task_t* iree_task_list_front(iree_task_list_t* list) { return list->head; }

iree_task_t* iree_task_list_back(iree_task_list_t* list) { return list->tail; }

void iree_task_list_push_back(iree_task_list_t* list, iree_task_t* task) {
  if (!list->head) {
    list->head = task;
  }
  if (list->tail) {
    list->tail->next_task = task;
  }
  list->tail = task;
  task->next_task = NULL;
}

void iree_task_list_push_front(iree_task_list_t* list, iree_task_t* task) {
  task->next_task = list->head;
  list->head = task;
  if (!list->tail) {
    list->tail = task;
  }
}

iree_task_t* iree_task_list_pop_front(iree_task_list_t* list) {
  if (!list->head) return NULL;
  iree_task_t* task = list->head;
  list->head = task->next_task;
  if (list->tail == task) {
    list->tail = NULL;
  }
  task->next_task = NULL;
  return task;
}

void iree_task_list_erase(iree_task_list_t* list, iree_task_t* prev_task,
                          iree_task_t* task) {
  if (task == list->head) {
    // Removing head.
    list->head = task->next_task;
  } else if (task == list->tail) {
    // Removing tail.
    list->tail = prev_task;
    prev_task->next_task = NULL;
  } else {
    // Removing inner.
    prev_task->next_task = task->next_task;
  }
  task->next_task = NULL;
}

void iree_task_list_prepend(iree_task_list_t* list, iree_task_list_t* prefix) {
  if (iree_task_list_is_empty(prefix)) return;
  if (iree_task_list_is_empty(list)) {
    list->head = prefix->head;
    list->tail = prefix->tail;
  } else {
    prefix->tail->next_task = list->head;
    list->head = prefix->head;
  }
  memset(prefix, 0, sizeof(*prefix));
}

void iree_task_list_append(iree_task_list_t* list, iree_task_list_t* suffix) {
  if (iree_task_list_is_empty(suffix)) return;
  if (iree_task_list_is_empty(list)) {
    list->head = suffix->head;
    list->tail = suffix->tail;
  } else {
    list->tail->next_task = suffix->head;
    list->tail = suffix->tail;
  }
  memset(suffix, 0, sizeof(*suffix));
}

void iree_task_list_append_from_fifo_slist(iree_task_list_t* list,
                                           iree_atomic_task_slist_t* slist) {
  iree_task_list_t suffix;
  iree_task_list_initialize(&suffix);
  if (!iree_atomic_task_slist_flush(
          slist, IREE_ATOMIC_SLIST_FLUSH_ORDER_APPROXIMATE_FIFO, &suffix.head,
          &suffix.tail)) {
    return;  // empty
  }
  iree_task_list_append(list, &suffix);
}

void iree_task_list_reverse(iree_task_list_t* list) {
  if (iree_task_list_is_empty(list)) return;
  iree_task_t* tail = list->head;
  iree_task_t* head = list->tail;
  iree_task_t* p = list->head;
  do {
    iree_task_t* next = p->next_task;
    p->next_task = head;
    head = p;
    p = next;
  } while (p != NULL);
  tail->next_task = NULL;
  list->head = head;
  list->tail = tail;
}

void iree_task_list_split(iree_task_list_t* head_list,
                          iree_host_size_t max_tasks,
                          iree_task_list_t* out_tail_list) {
  iree_task_list_initialize(out_tail_list);
  if (head_list->head == NULL) return;
  if (head_list->head == head_list->tail) {
    // 1 task in the source list; always prefer to steal it.
    // This is because the victim is likely working on their last item and we
    // can help them out by popping this off. It also has the side-effect of
    // handling cases of donated workers wanting to steal all tasks to
    // synchronously execute things.
    iree_task_list_move(head_list, out_tail_list);
    return;
  }

  // Walk through the |head_list| with two iterators; one at double-rate.
  // If we ever notice this function showing up in profiling then we should
  // build an acceleration structure to avoid the full walk of the first half
  // (e.g. skip list).
  iree_task_t* p_x1_m1 = head_list->head;  // p_x1 - 1 (previous to p_x1)
  iree_task_t* p_x1 = head_list->head;     // x1 speed ptr
  iree_task_t* p_x2 = head_list->head;     // x2 speed ptr
  while (p_x2->next_task != NULL) {
    p_x1_m1 = p_x1;
    p_x1 = p_x1->next_task;
    p_x2 = p_x2->next_task;
    if (p_x2->next_task) p_x2 = p_x2->next_task;
  }

  // p_x1 now points at the half way point in the head_list. This is where we
  // *start* our windowed walk for pulling out max_tasks, implicitly limiting us
  // to take at most half of the tasks from the list.

  // Advance the tail list keeping an iterator -max_tasks back; when we hit the
  // end we have our head and tail to form the list.
  iree_task_t* p_window_prev = p_x1_m1;
  iree_task_t* p_window_head = p_x1;
  iree_task_t* p_window_tail = p_x1;
  while (p_window_tail->next_task != NULL && --max_tasks > 0) {
    p_window_tail = p_window_tail->next_task;
  }
  while (p_window_tail->next_task != NULL) {
    p_window_prev = p_window_head;
    p_window_head = p_window_head->next_task;
    p_window_tail = p_window_tail->next_task;
  }

  head_list->tail = p_window_prev;
  p_window_prev->next_task = NULL;

  out_tail_list->head = p_window_head;
  out_tail_list->tail = p_window_tail;
}
