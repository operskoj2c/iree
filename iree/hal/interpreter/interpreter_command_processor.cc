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

#include "iree/hal/interpreter/interpreter_command_processor.h"

#include "absl/container/inlined_vector.h"
#include "absl/types/span.h"
#include "iree/base/source_location.h"
#include "iree/base/status.h"
#include "iree/base/tracing.h"
#include "iree/hal/buffer_view.h"
#include "iree/hal/interpreter/bytecode_executable.h"

namespace iree {
namespace hal {

InterpreterCommandProcessor::InterpreterCommandProcessor(
    Allocator* allocator, CommandBufferModeBitfield mode,
    CommandCategoryBitfield command_categories)
    : HostLocalCommandProcessor(allocator, mode, command_categories) {}

InterpreterCommandProcessor::~InterpreterCommandProcessor() = default;

Status InterpreterCommandProcessor::Dispatch(
    const DispatchRequest& dispatch_request) {
  IREE_TRACE_SCOPE0("InterpreterCommandProcessor::Dispatch");

  // Lookup the exported function.
  auto* executable =
      static_cast<BytecodeExecutable*>(dispatch_request.executable);
  const auto& module = executable->module();
  ASSIGN_OR_RETURN(auto entry_function, module.function_table().LookupExport(
                                            dispatch_request.entry_point));

  vm::Stack stack;

  // TODO(benvanik): avoid this by directly referencing the bindings.
  absl::InlinedVector<BufferView, 8> args;
  args.reserve(dispatch_request.bindings.size());
  for (auto& binding : dispatch_request.bindings) {
    args.push_back(BufferView{add_ref(binding.buffer), binding.shape,
                              binding.element_size});
  }
  absl::InlinedVector<BufferView, 8> results;
  if (entry_function.result_count() > 0) {
    return UnimplementedErrorBuilder(IREE_LOC)
           << "Executable export results are not yet implemented";
  }

  RETURN_IF_ERROR(executable->context().Invoke(
      &stack, entry_function, absl::MakeSpan(args), absl::MakeSpan(results)));

  return OkStatus();
}

}  // namespace hal
}  // namespace iree
