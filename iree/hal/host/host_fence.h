// Copyright 2019 Google LLC
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

#ifndef IREE_HAL_HOST_HOST_FENCE_H_
#define IREE_HAL_HOST_HOST_FENCE_H_

#include <atomic>
#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "iree/base/status.h"
#include "iree/hal/fence.h"

namespace iree {
namespace hal {

// TODO(b/140026716): add WaitHandle support for better multi-wait.
// Simple host-only fence semaphore implemented with a mutex.
//
// Thread-safe (as instances may be imported and used by others).
class HostFence final : public Fence {
 public:
  // Waits for one or more (or all) fences to reach or exceed the given values.
  static Status WaitForFences(absl::Span<const FenceValue> fences,
                              bool wait_all, absl::Time deadline);

  explicit HostFence(uint64_t initial_value);
  ~HostFence() override;

  Status status() const override;
  StatusOr<uint64_t> QueryValue() override;

  Status Signal(uint64_t value);
  Status Fail(Status status);

 private:
  // The mutex is not required to query the value; this lets us quickly check if
  // a required value has been exceeded. The mutex is only used to update and
  // notify waiters.
  std::atomic<uint64_t> value_{0};

  // We have a full mutex here so that we can perform condvar waits on value
  // changes.
  mutable absl::Mutex mutex_;
  Status status_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace hal
}  // namespace iree

#endif  // IREE_HAL_HOST_HOST_FENCE_H_
