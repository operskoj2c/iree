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

#include "iree/base/threading.h"

#include <chrono>
#include <thread>

#include "iree/base/synchronization.h"
#include "iree/base/threading_impl.h"  // to test the override list
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace {

using iree::Status;

//==============================================================================
// iree_thread_t
//==============================================================================

TEST(ThreadTest, Lifetime) {
  // Default parameters:
  iree_thread_create_params_t params;
  memset(&params, 0, sizeof(params));

  // Our thread: do a bit of math and notify the main test thread when done.
  struct entry_data_s {
    iree_atomic_int32_t value;
    iree_notification_t barrier;
  } entry_data;
  iree_atomic_store_int32(&entry_data.value, 123, iree_memory_order_relaxed);
  iree_notification_initialize(&entry_data.barrier);
  iree_thread_entry_t entry_fn = +[](void* entry_arg) -> int {
    auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
    iree_atomic_fetch_add_int32(&entry_data->value, 1,
                                iree_memory_order_acq_rel);
    iree_notification_post(&entry_data->barrier, IREE_ALL_WAITERS);
    return 0;
  };

  // Create the thread and immediately begin running it.
  iree_thread_t* thread = nullptr;
  IREE_ASSERT_OK(Status(iree_thread_create(entry_fn, &entry_data, params,
                                           iree_allocator_system(), &thread)));
  EXPECT_NE(0, iree_thread_id(thread));

  // Drop the thread handle; should be safe as the thread should keep itself
  // retained as long as it needs to.
  iree_thread_release(thread);

  // Wait for the thread to finish.
  iree_notification_await(
      &entry_data.barrier,
      +[](void* entry_arg) -> bool {
        auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
        return iree_atomic_load_int32(&entry_data->value,
                                      iree_memory_order_relaxed) == (123 + 1);
      },
      &entry_data);
  iree_notification_deinitialize(&entry_data.barrier);
}

TEST(ThreadTest, CreateSuspended) {
  iree_thread_create_params_t params;
  memset(&params, 0, sizeof(params));
  params.create_suspended = true;

  struct entry_data_s {
    iree_atomic_int32_t value;
    iree_notification_t barrier;
  } entry_data;
  iree_atomic_store_int32(&entry_data.value, 123, iree_memory_order_relaxed);
  iree_notification_initialize(&entry_data.barrier);
  iree_thread_entry_t entry_fn = +[](void* entry_arg) -> int {
    auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
    iree_atomic_fetch_add_int32(&entry_data->value, 1,
                                iree_memory_order_acq_rel);
    iree_notification_post(&entry_data->barrier, IREE_ALL_WAITERS);
    return 0;
  };

  iree_thread_t* thread = nullptr;
  IREE_ASSERT_OK(Status(iree_thread_create(entry_fn, &entry_data, params,
                                           iree_allocator_system(), &thread)));
  EXPECT_NE(0, iree_thread_id(thread));

  // NOTE: the thread will not be running and we should not expect a change in
  // the value. I can't think of a good way to test this, though, so we'll just
  // wait a moment here and assume that if the thread was able to run it would
  // have during this wait.
  ASSERT_EQ(123, iree_atomic_load_int32(&entry_data.value,
                                        iree_memory_order_seq_cst));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  ASSERT_EQ(123, iree_atomic_load_int32(&entry_data.value,
                                        iree_memory_order_seq_cst));

  // Resume the thread and wait for it to finish its work.
  iree_thread_resume(thread);
  iree_notification_await(
      &entry_data.barrier,
      +[](void* entry_arg) -> bool {
        auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
        return iree_atomic_load_int32(&entry_data->value,
                                      iree_memory_order_relaxed) == (123 + 1);
      },
      &entry_data);
  iree_notification_deinitialize(&entry_data.barrier);
  iree_thread_release(thread);
}

// NOTE: testing whether priority took effect is really hard given that on
// certain platforms the priority may not be respected or may be clamped by
// the system. This is here to test the mechanics of the priority override code
// on our side and assumes that if we tell the OS something it respects it.
TEST(ThreadTest, PriorityOverride) {
  iree_thread_create_params_t params;
  memset(&params, 0, sizeof(params));

  struct entry_data_s {
    iree_atomic_int32_t value;
    iree_notification_t barrier;
  } entry_data;
  iree_atomic_store_int32(&entry_data.value, 0, iree_memory_order_relaxed);
  iree_notification_initialize(&entry_data.barrier);
  iree_thread_entry_t entry_fn = +[](void* entry_arg) -> int {
    auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
    iree_atomic_fetch_add_int32(&entry_data->value, 1,
                                iree_memory_order_acq_rel);
    iree_notification_post(&entry_data->barrier, IREE_ALL_WAITERS);
    return 0;
  };

  iree_thread_t* thread = nullptr;
  IREE_ASSERT_OK(Status(iree_thread_create(entry_fn, &entry_data, params,
                                           iree_allocator_system(), &thread)));
  EXPECT_NE(0, iree_thread_id(thread));

  // Push a few overrides.
  iree_thread_override_t* override0 = iree_thread_priority_class_override_begin(
      thread, IREE_THREAD_PRIORITY_CLASS_HIGH);
  EXPECT_NE(nullptr, override0);
  iree_thread_override_t* override1 = iree_thread_priority_class_override_begin(
      thread, IREE_THREAD_PRIORITY_CLASS_HIGHEST);
  EXPECT_NE(nullptr, override1);
  iree_thread_override_t* override2 = iree_thread_priority_class_override_begin(
      thread, IREE_THREAD_PRIORITY_CLASS_LOWEST);
  EXPECT_NE(nullptr, override2);

  // Wait for the thread to finish.
  iree_notification_await(
      &entry_data.barrier,
      +[](void* entry_arg) -> bool {
        auto* entry_data = reinterpret_cast<struct entry_data_s*>(entry_arg);
        return iree_atomic_load_int32(&entry_data->value,
                                      iree_memory_order_relaxed) == 1;
      },
      &entry_data);
  iree_notification_deinitialize(&entry_data.barrier);

  // Pop overrides (in opposite order intentionally).
  iree_thread_override_end(override0);
  iree_thread_override_end(override1);
  iree_thread_override_end(override2);

  iree_thread_release(thread);
}

//==============================================================================
// iree_thread_override_list_t
//==============================================================================
// This is an implementation detail but useful to test on its own as it's shared
// across several platform implementations.

TEST(ThreadOverrideListTest, PriorityClass) {
  static iree_thread_t* kThreadSentinel =
      reinterpret_cast<iree_thread_t*>(0x123);
  static iree_thread_priority_class_t current_priority_class =
      IREE_THREAD_PRIORITY_CLASS_NORMAL;
  iree_thread_override_list_t list;
  iree_thread_override_list_initialize(
      +[](iree_thread_t* thread, iree_thread_priority_class_t priority_class) {
        EXPECT_EQ(kThreadSentinel, thread);
        EXPECT_NE(current_priority_class, priority_class);
        current_priority_class = priority_class;
      },
      current_priority_class, iree_allocator_system(), &list);

  // (NORMAL) -> HIGH -> [ignored LOW] -> HIGHEST
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_NORMAL, current_priority_class);
  iree_thread_override_t* override0 = iree_thread_override_list_add(
      &list, kThreadSentinel, IREE_THREAD_PRIORITY_CLASS_HIGH);
  EXPECT_NE(nullptr, override0);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGH, current_priority_class);
  iree_thread_override_t* override1 = iree_thread_override_list_add(
      &list, kThreadSentinel, IREE_THREAD_PRIORITY_CLASS_LOW);
  EXPECT_NE(nullptr, override1);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGH, current_priority_class);
  iree_thread_override_t* override2 = iree_thread_override_list_add(
      &list, kThreadSentinel, IREE_THREAD_PRIORITY_CLASS_HIGHEST);
  EXPECT_NE(nullptr, override2);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGHEST, current_priority_class);

  // Out of order to ensure highest bit sticks:
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGHEST, current_priority_class);
  iree_thread_override_remove_self(override1);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGHEST, current_priority_class);
  iree_thread_override_remove_self(override0);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_HIGHEST, current_priority_class);
  iree_thread_override_remove_self(override2);
  ASSERT_EQ(IREE_THREAD_PRIORITY_CLASS_NORMAL, current_priority_class);

  iree_thread_override_list_deinitialize(&list);
}

//==============================================================================
// iree_fpu_state_*
//==============================================================================

// NOTE: depending on compiler options or architecture denormals may always be
// flushed to zero. Here we just test that they are flushed when we request them
// to be.
TEST(FPUStateTest, FlushDenormalsToZero) {
  iree_fpu_state_t fpu_state =
      iree_fpu_state_push(IREE_FPU_STATE_FLAG_FLUSH_DENORMALS_TO_ZERO);

  float f = 1.0f;
  volatile float* fp = &f;
  *fp = *fp * 1e-39f;
  EXPECT_EQ(0.0f, f);

  iree_fpu_state_pop(fpu_state);
}

}  // namespace
