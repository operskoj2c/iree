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

#include "iree/vm/debug/debug_client.h"

#include "absl/types/source_location.h"
#include "iree/base/status.h"

namespace iree {
namespace vm {
namespace debug {

Status DebugClient::GetFunction(
    std::string module_name, std::string function_name,
    std::function<void(StatusOr<RemoteFunction*> function)> callback) {
  return ResolveFunction(
      module_name, function_name,
      [this, module_name, callback](StatusOr<int> function_ordinal) {
        if (!function_ordinal.ok()) {
          callback(function_ordinal.status());
          return;
        }
        auto status =
            GetFunction(module_name, function_ordinal.ValueOrDie(), callback);
        if (!status.ok()) {
          callback(std::move(status));
        }
      });
}

Status DebugClient::StepFiberOver(const RemoteFiberState& fiber_state,
                                  std::function<void()> callback) {
  // TODO(benvanik): implement bytecode stepping search.
  // int bytecode_offset = 0;
  // return StepFiberToOffset(fiber_state, bytecode_offset,
  // std::move(callback));
  return UnimplementedErrorBuilder(ABSL_LOC)
         << "StepFiberOver not yet implemented";
}

Status DebugClient::StepFiberOut(const RemoteFiberState& fiber_state,
                                 std::function<void()> callback) {
  // TODO(benvanik): implement bytecode stepping search.
  // int bytecode_offset = 0;
  // return StepFiberToOffset(fiber_state, bytecode_offset,
  // std::move(callback));
  return UnimplementedErrorBuilder(ABSL_LOC)
         << "StepFiberOut not yet implemented";
}

}  // namespace debug
}  // namespace vm
}  // namespace iree
