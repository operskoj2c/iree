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

#include "iree/vm/debug/debug_service.h"

#include <algorithm>
#include <memory>

#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"
#include "third_party/flatbuffers/include/flatbuffers/reflection.h"
#include "iree/base/flatbuffer_util.h"
#include "iree/base/source_location.h"
#include "iree/base/status.h"
#include "iree/schemas/debug_service_generated.h"
#include "iree/schemas/reflection_data.h"
#include "iree/vm/instance.h"

namespace iree {
namespace vm {
namespace debug {
namespace {

using ::flatbuffers::FlatBufferBuilder;
using ::flatbuffers::Offset;
using ::iree::hal::BufferView;
using ::iree::vm::Module;
using ::iree::vm::StackFrame;

// Gets an embedded flatbuffers reflection schema.
const ::reflection::Schema& GetSchema(const char* schema_name) {
  for (const auto* file_toc = schemas::reflection_data_create();
       file_toc != nullptr; ++file_toc) {
    if (std::strcmp(file_toc->name, schema_name) == 0) {
      return *::reflection::GetSchema(file_toc->data);
    }
  }
  LOG(FATAL) << "FlatBuffer schema '" << schema_name
             << "' not found in binary; ensure it is in :reflection_data";
}

// Recursively copies a flatbuffer table, returning the root offset in |fbb|.
template <typename T>
StatusOr<Offset<T>> DeepCopyTable(const char* schema_name, const T& table_def,
                                  FlatBufferBuilder* fbb) {
  const auto* root_table =
      reinterpret_cast<const ::flatbuffers::Table*>(std::addressof(table_def));
  const auto& schema = GetSchema(schema_name);
  return {::flatbuffers::CopyTable(*fbb, schema, *schema.root_table(),
                                   *root_table,
                                   /*use_string_pooling=*/false)
              .o};
}

// Serializes a buffer_view value, optionally including the entire buffer
// contents.
StatusOr<Offset<rpc::BufferViewDef>> SerializeBufferView(
    const BufferView& buffer_view, bool include_buffer_contents,
    FlatBufferBuilder* fbb) {
  auto shape_offs = fbb->CreateVector(buffer_view.shape.subspan().data(),
                                      buffer_view.shape.subspan().size());
  rpc::BufferViewDefBuilder value(*fbb);
  value.add_is_valid(buffer_view.buffer != nullptr);
  value.add_shape(shape_offs);
  value.add_element_size(buffer_view.element_size);
  if (include_buffer_contents) {
    // TODO(benvanik): add buffer data.
  }
  return value.Finish();
}

// Serializes a stack frame.
StatusOr<Offset<rpc::StackFrameDef>> SerializeStackFrame(
    const StackFrame& stack_frame, FlatBufferBuilder* fbb) {
  ASSIGN_OR_RETURN(int function_ordinal,
                   stack_frame.module().function_table().LookupFunctionOrdinal(
                       stack_frame.function()));
  auto module_name_offs = fbb->CreateString(stack_frame.module().name().data(),
                                            stack_frame.module().name().size());
  std::vector<Offset<rpc::BufferViewDef>> local_offs_list;
  for (const auto& local : stack_frame.locals()) {
    ASSIGN_OR_RETURN(
        auto local_offs,
        SerializeBufferView(local, /*include_buffer_contents=*/false, fbb));
    local_offs_list.push_back(local_offs);
  }
  auto locals_offs = fbb->CreateVector(local_offs_list);
  rpc::StackFrameDefBuilder sfb(*fbb);
  sfb.add_module_name(module_name_offs);
  sfb.add_function_ordinal(function_ordinal);
  sfb.add_offset(stack_frame.offset());
  sfb.add_locals(locals_offs);
  return sfb.Finish();
}

// Resolves a local from a fiber:frame:local_index to a BufferView.
StatusOr<BufferView*> ResolveFiberLocal(FiberState* fiber_state,
                                        int frame_index, int local_index) {
  auto frames = fiber_state->mutable_stack()->mutable_frames();
  if (frame_index < 0 || frame_index > frames.size()) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Frame index " << frame_index << " out of bounds ("
           << frames.size() << ")";
  }
  auto locals = frames[frame_index].mutable_locals();
  if (local_index < 0 || local_index > locals.size()) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Local index " << local_index << " out of bounds ("
           << locals.size() << ")";
  }
  return &locals[local_index];
}

// Suspends a set of fibers and blocks until all have been suspended (or one or
// more fails to suspend).
// This works only when the caller is *not* one of the threads executing a
// fiber in |fiber_states| (this normally shouldn't happen, but may if we
// support eval()-like semantics).
Status SuspendFibersAndWait(absl::Span<FiberState*> fiber_states) {
  absl::Mutex suspend_mutex;
  Status one_suspend_status = OkStatus();
  std::list<int> pending_suspend_ids;
  for (auto* fiber_state : fiber_states) {
    pending_suspend_ids.push_back(fiber_state->id());
  }
  for (auto* fiber_state : fiber_states) {
    auto suspend_callback = [&, fiber_state](Status suspend_status) {
      absl::MutexLock lock(&suspend_mutex);
      auto it = std::find(pending_suspend_ids.begin(),
                          pending_suspend_ids.end(), fiber_state->id());
      CHECK(it != pending_suspend_ids.end());
      pending_suspend_ids.erase(it);
      if (!suspend_status.ok()) {
        one_suspend_status = std::move(suspend_status);
      }
    };
    RETURN_IF_ERROR(fiber_state->Suspend(suspend_callback));
  }
  suspend_mutex.LockWhen(absl::Condition(
      +[](std::list<int>* pending_suspend_ids) {
        return pending_suspend_ids->empty();
      },
      &pending_suspend_ids));
  suspend_mutex.Unlock();
  return one_suspend_status;
}

}  // namespace

Status DebugService::SuspendAllFibers() {
  VLOG(2) << "SuspendAllFibers";
  for (auto* fiber_state : fiber_states_) {
    RETURN_IF_ERROR(fiber_state->Suspend());
  }
  return OkStatus();
}

Status DebugService::ResumeAllFibers() {
  VLOG(2) << "ResumeAllFibers";
  for (auto* fiber_state : fiber_states_) {
    RETURN_IF_ERROR(fiber_state->Resume());
  }
  return OkStatus();
}

Status DebugService::RegisterContext(SequencerContext* context) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "RegisterContext(" << context->id() << ")";
  RETURN_IF_ERROR(SuspendAllFibers());
  RETURN_IF_ERROR(UnreadyAllSessions());
  contexts_.push_back(context);
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnContextRegistered(context));
  }
  RETURN_IF_ERROR(ResumeAllFibers());
  return OkStatus();
}

Status DebugService::UnregisterContext(SequencerContext* context) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "UnregisterContext(" << context->id() << ")";
  auto it = std::find(contexts_.begin(), contexts_.end(), context);
  if (it == contexts_.end()) {
    return NotFoundErrorBuilder(IREE_LOC) << "Context not registered";
  }
  RETURN_IF_ERROR(SuspendAllFibers());
  RETURN_IF_ERROR(UnreadyAllSessions());
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnContextUnregistered(context));
  }
  contexts_.erase(it);
  RETURN_IF_ERROR(ResumeAllFibers());
  return OkStatus();
}

StatusOr<SequencerContext*> DebugService::GetContext(int context_id) const {
  for (auto* context : contexts_) {
    if (context->id() == context_id) {
      return context;
    }
  }
  return NotFoundErrorBuilder(IREE_LOC)
         << "Context with ID " << context_id
         << " not registered with the debug service";
}

Status DebugService::RegisterContextModule(SequencerContext* context,
                                           Module* module) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "RegisterContextModule(" << context->id() << ", " << module->name()
          << ")";
  RETURN_IF_ERROR(SuspendAllFibers());
  RETURN_IF_ERROR(UnreadyAllSessions());
  RETURN_IF_ERROR(RegisterModuleBreakpoints(context, module));
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnModuleLoaded(context, module));
  }
  RETURN_IF_ERROR(ResumeAllFibers());
  return OkStatus();
}

StatusOr<Module*> DebugService::GetModule(int context_id,
                                          absl::string_view module_name) const {
  ASSIGN_OR_RETURN(auto* context, GetContext(context_id));
  for (const auto& module : context->modules()) {
    if (module->name() == module_name) {
      return module.get();
    }
  }
  return NotFoundErrorBuilder(IREE_LOC)
         << "Module '" << module_name << "' not found on context "
         << context_id;
}

Status DebugService::RegisterFiberState(FiberState* fiber_state) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "RegisterFiberState(" << fiber_state->id() << ")";
  RETURN_IF_ERROR(SuspendAllFibers());
  RETURN_IF_ERROR(UnreadyAllSessions());
  fiber_states_.push_back(fiber_state);
  if (sessions_unready_) {
    // Suspend immediately as a debugger is not yet read.
    RETURN_IF_ERROR(fiber_state->Suspend());
  }
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnFiberRegistered(fiber_state));
  }
  RETURN_IF_ERROR(ResumeAllFibers());
  return OkStatus();
}

Status DebugService::UnregisterFiberState(FiberState* fiber_state) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "UnregisterFiberState(" << fiber_state->id() << ")";
  auto it = std::find(fiber_states_.begin(), fiber_states_.end(), fiber_state);
  if (it == fiber_states_.end()) {
    return NotFoundErrorBuilder(IREE_LOC) << "Fiber state not registered";
  }
  RETURN_IF_ERROR(SuspendAllFibers());
  RETURN_IF_ERROR(UnreadyAllSessions());
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnFiberUnregistered(fiber_state));
  }
  fiber_states_.erase(it);
  RETURN_IF_ERROR(ResumeAllFibers());
  return OkStatus();
}

StatusOr<FiberState*> DebugService::GetFiberState(int fiber_id) const {
  for (auto* fiber_state : fiber_states_) {
    if (fiber_state->id() == fiber_id) {
      return fiber_state;
    }
  }
  return NotFoundErrorBuilder(IREE_LOC)
         << "Fiber state with ID " << fiber_id
         << " not registered with the debug service";
}

StatusOr<Offset<rpc::FiberStateDef>> DebugService::SerializeFiberState(
    const FiberState& fiber_state, FlatBufferBuilder* fbb) {
  std::vector<Offset<rpc::StackFrameDef>> frame_offs_list;
  for (const auto& frame : fiber_state.stack().frames()) {
    ASSIGN_OR_RETURN(auto frame_offs, SerializeStackFrame(frame, fbb));
    frame_offs_list.push_back(frame_offs);
  }
  auto frames_offs = fbb->CreateVector(frame_offs_list);
  rpc::FiberStateDefBuilder fsb(*fbb);
  fsb.add_fiber_id(fiber_state.id());
  fsb.add_frames(frames_offs);
  return fsb.Finish();
}

Status DebugService::RegisterDebugSession(DebugSession* session) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "RegisterDebugSession(" << session->id() << ")";
  sessions_.push_back(session);
  if (session->is_ready()) {
    ++sessions_ready_;
  } else {
    // Immediately suspend all fibers until the session readies up (or
    // disconnects).
    ++sessions_unready_;
    RETURN_IF_ERROR(SuspendAllFibers());
  }
  return OkStatus();
}

Status DebugService::UnregisterDebugSession(DebugSession* session) {
  absl::MutexLock lock(&mutex_);
  VLOG(2) << "UnregisterDebugSession(" << session->id() << ")";
  auto it = std::find(sessions_.begin(), sessions_.end(), session);
  if (it == sessions_.end()) {
    return NotFoundErrorBuilder(IREE_LOC) << "Session not registered";
  }
  sessions_.erase(it);
  if (session->is_ready()) {
    --sessions_ready_;
  } else {
    // If the session never readied up then we still have all fibers suspended
    // waiting for it. We should resume so that we don't block forever.
    --sessions_unready_;
    RETURN_IF_ERROR(ResumeAllFibers());
  }
  return OkStatus();
}

Status DebugService::WaitUntilAllSessionsReady() {
  VLOG(1) << "Waiting until all sessions are ready...";
  struct CondState {
    DebugService* service;
    bool had_sessions;
    bool consider_aborted;
  } cond_state;
  {
    absl::MutexLock lock(&mutex_);
    cond_state.service = this;
    cond_state.had_sessions = !sessions_.empty();
    cond_state.consider_aborted = false;
  }
  mutex_.LockWhen(absl::Condition(
      +[](CondState* cond_state) {
        cond_state->service->mutex_.AssertHeld();
        if (cond_state->service->sessions_ready_ > 0) {
          // One or more sessions are ready.
          return true;
        }
        if (cond_state->service->sessions_unready_ > 0) {
          // One or more sessions are connected but not yet ready.
          cond_state->had_sessions = true;
          return false;
        }
        if (cond_state->had_sessions &&
            cond_state->service->sessions_.empty()) {
          // We had sessions but now we don't, consider this an error and bail.
          // This can happen when a session connects but never readies up.
          cond_state->consider_aborted = true;
          return true;
        }
        return false;
      },
      &cond_state));
  mutex_.Unlock();
  if (cond_state.consider_aborted) {
    return AbortedErrorBuilder(IREE_LOC)
           << "At least one session connected but never readied up";
  }
  VLOG(1) << "Sessions ready, resuming";
  return OkStatus();
}

StatusOr<Offset<rpc::MakeReadyResponse>> DebugService::MakeReady(
    const rpc::MakeReadyRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: MakeReady()";
  // TODO(benvanik): support more than one session.
  CHECK_LE(sessions_.size(), 1) << "Only one session is currently supported";
  if (!sessions_.empty()) {
    RETURN_IF_ERROR(sessions_[0]->OnReady());
  }
  sessions_ready_ = 0;
  sessions_unready_ = 0;
  for (auto* session : sessions_) {
    sessions_ready_ += session->is_ready() ? 1 : 0;
    sessions_unready_ += session->is_ready() ? 0 : 1;
  }
  rpc::MakeReadyResponseBuilder response(*fbb);
  return response.Finish();
}

Status DebugService::UnreadyAllSessions() {
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnUnready());
  }
  sessions_ready_ = 0;
  sessions_unready_ = sessions_.size();
  return OkStatus();
}

StatusOr<Offset<rpc::GetStatusResponse>> DebugService::GetStatus(
    const rpc::GetStatusRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: GetStatus()";
  rpc::GetStatusResponseBuilder response(*fbb);
  response.add_protocol(0);
  return response.Finish();
}

StatusOr<Offset<rpc::ListContextsResponse>> DebugService::ListContexts(
    const rpc::ListContextsRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: ListContexts()";
  std::vector<Offset<rpc::ContextDef>> context_offs;
  for (auto* context : contexts_) {
    std::vector<Offset<rpc::NativeFunctionDef>> native_function_offs_list;
    for (const auto& pair : context->native_functions()) {
      auto name_offs = fbb->CreateString(pair.first);
      rpc::NativeFunctionDefBuilder native_function(*fbb);
      native_function.add_name(name_offs);
      native_function_offs_list.push_back(native_function.Finish());
    }
    auto native_functions_offs = fbb->CreateVector(native_function_offs_list);

    std::vector<std::string> module_names;
    for (const auto& module : context->modules()) {
      module_names.push_back(std::string(module->name()));
    }
    auto module_names_offs = fbb->CreateVectorOfStrings(module_names);

    rpc::ContextDefBuilder context_def(*fbb);
    context_def.add_context_id(context->id());
    context_def.add_native_functions(native_functions_offs);
    context_def.add_module_names(module_names_offs);
    context_offs.push_back(context_def.Finish());
  }

  auto contexts_offs = fbb->CreateVector(context_offs);
  rpc::ListContextsResponseBuilder response(*fbb);
  response.add_contexts(contexts_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::GetModuleResponse>> DebugService::GetModule(
    const rpc::GetModuleRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: GetModule(" << request.context_id() << ", "
          << WrapString(request.module_name()) << ")";
  ASSIGN_OR_RETURN(auto* module, GetModule(request.context_id(),
                                           WrapString(request.module_name())));
  // TODO(benvanik): find a way to do this without possibly duping all memory.
  // I suspect that when we make constants poolable then there's only one
  // place to kill and there may be magic we could use to do that during a
  // reflection pass.
  ModuleDefT module_t;
  module->def().UnPackTo(&module_t);
  for (auto& function : module_t.function_table->functions) {
    function->bytecode->contents.clear();
  }
  auto trimmed_module_offs = ModuleDef::Pack(*fbb, &module_t);
  rpc::GetModuleResponseBuilder response(*fbb);
  response.add_module_(trimmed_module_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::GetFunctionResponse>> DebugService::GetFunction(
    const rpc::GetFunctionRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: GetFunction(" << WrapString(request.module_name()) << ", "
          << request.function_ordinal() << ")";
  ASSIGN_OR_RETURN(auto* module, GetModule(request.context_id(),
                                           WrapString(request.module_name())));
  ASSIGN_OR_RETURN(auto& function, module->function_table().LookupFunction(
                                       request.function_ordinal()));
  Offset<BytecodeDef> bytecode_offs;
  if (function.def().bytecode()) {
    ASSIGN_OR_RETURN(
        bytecode_offs,
        DeepCopyTable("bytecode_def.bfbs", *function.def().bytecode(), fbb));
  }
  rpc::GetFunctionResponseBuilder response(*fbb);
  response.add_bytecode(bytecode_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::ResolveFunctionResponse>> DebugService::ResolveFunction(
    const rpc::ResolveFunctionRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: ResolveFunction(" << WrapString(request.module_name())
          << ", " << WrapString(request.function_name()) << ")";
  std::vector<int32_t> context_ids;
  auto context_ids_offs = fbb->CreateVector(context_ids);
  int function_ordinal = -1;
  for (auto* context : contexts_) {
    for (const auto& module : context->modules()) {
      if (module->name() == WrapString(request.module_name())) {
        ASSIGN_OR_RETURN(function_ordinal,
                         module->function_table().LookupFunctionOrdinalByName(
                             WrapString(request.function_name())));
        context_ids.push_back(context->id());
        break;
      }
    }
  }
  rpc::ResolveFunctionResponseBuilder response(*fbb);
  response.add_context_ids(context_ids_offs);
  response.add_function_ordinal(function_ordinal);
  return response.Finish();
}

StatusOr<Offset<rpc::ListFibersResponse>> DebugService::ListFibers(
    const rpc::ListFibersRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: ListFibers()";
  std::vector<Offset<rpc::FiberStateDef>> fiber_state_offsets;
  for (auto* fiber_state : fiber_states_) {
    ASSIGN_OR_RETURN(auto fiber_state_offs,
                     SerializeFiberState(*fiber_state, fbb));
    fiber_state_offsets.push_back(fiber_state_offs);
  }
  auto fiber_states_offs = fbb->CreateVector(fiber_state_offsets);
  rpc::ListFibersResponseBuilder response(*fbb);
  response.add_fiber_states(fiber_states_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::SuspendFibersResponse>> DebugService::SuspendFibers(
    const rpc::SuspendFibersRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: SuspendFibers(fiber_ids=["
          << (request.fiber_ids() ? absl::StrJoin(*request.fiber_ids(), ", ")
                                  : "")
          << "])";
  std::vector<Offset<rpc::FiberStateDef>> fiber_state_offsets;
  if (request.fiber_ids() && request.fiber_ids()->size() > 0) {
    // Suspending a list of fibers.
    std::vector<FiberState*> fibers_to_suspend;
    for (int fiber_id : *request.fiber_ids()) {
      ASSIGN_OR_RETURN(auto* fiber_state, GetFiberState(fiber_id));
      fibers_to_suspend.push_back(fiber_state);
    }
    RETURN_IF_ERROR(SuspendFibersAndWait(absl::MakeSpan(fibers_to_suspend)));
    for (auto* fiber_state : fibers_to_suspend) {
      ASSIGN_OR_RETURN(auto fiber_state_offs,
                       SerializeFiberState(*fiber_state, fbb));
      fiber_state_offsets.push_back(fiber_state_offs);
    }
  } else {
    // Suspending all fibers.
    RETURN_IF_ERROR(SuspendAllFibers());
    for (auto* fiber_state : fiber_states_) {
      ASSIGN_OR_RETURN(auto fiber_state_offs,
                       SerializeFiberState(*fiber_state, fbb));
      fiber_state_offsets.push_back(fiber_state_offs);
    }
  }
  auto fiber_states_offs = fbb->CreateVector(fiber_state_offsets);
  rpc::SuspendFibersResponseBuilder response(*fbb);
  response.add_fiber_states(fiber_states_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::ResumeFibersResponse>> DebugService::ResumeFibers(
    const rpc::ResumeFibersRequest& request, FlatBufferBuilder* fbb) {
  VLOG(1) << "RPC: ResumeFibers(fiber_ids=["
          << (request.fiber_ids() ? absl::StrJoin(*request.fiber_ids(), ", ")
                                  : "")
          << "])";
  absl::MutexLock lock(&mutex_);
  if (request.fiber_ids() && request.fiber_ids()->size() > 0) {
    // Resuming a list of fibers.
    for (int fiber_id : *request.fiber_ids()) {
      ASSIGN_OR_RETURN(auto* fiber_state, GetFiberState(fiber_id));
      RETURN_IF_ERROR(fiber_state->Resume());
    }
  } else {
    // Resuming all fibers.
    RETURN_IF_ERROR(ResumeAllFibers());
  }
  rpc::ResumeFibersResponseBuilder response(*fbb);
  return response.Finish();
}

StatusOr<Offset<rpc::StepFiberResponse>> DebugService::StepFiber(
    const rpc::StepFiberRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: StepFiber(" << request.fiber_id() << ")";
  ASSIGN_OR_RETURN(auto* fiber_state, GetFiberState(request.fiber_id()));
  FiberState::StepTarget step_target;
  // TODO(benvanik): step settings.
  RETURN_IF_ERROR(fiber_state->Step(step_target));
  rpc::StepFiberResponseBuilder response(*fbb);
  return response.Finish();
}

StatusOr<Offset<rpc::GetFiberLocalResponse>> DebugService::GetFiberLocal(
    const rpc::GetFiberLocalRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: GetFiberLocal(" << request.fiber_id() << ", "
          << request.frame_index() << ", " << request.local_index() << ")";
  ASSIGN_OR_RETURN(auto* fiber_state, GetFiberState(request.fiber_id()));
  ASSIGN_OR_RETURN(auto* local,
                   ResolveFiberLocal(fiber_state, request.frame_index(),
                                     request.local_index()));

  ASSIGN_OR_RETURN(
      auto value_offs,
      SerializeBufferView(*local, /*include_buffer_contents=*/true, fbb));
  rpc::GetFiberLocalResponseBuilder response(*fbb);
  response.add_value(value_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::SetFiberLocalResponse>> DebugService::SetFiberLocal(
    const rpc::SetFiberLocalRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: SetFiberLocal(" << request.fiber_id() << ", "
          << request.frame_index() << ", " << request.local_index() << ")";
  ASSIGN_OR_RETURN(auto* fiber_state, GetFiberState(request.fiber_id()));
  ASSIGN_OR_RETURN(auto* local,
                   ResolveFiberLocal(fiber_state, request.frame_index(),
                                     request.local_index()));

  if (!request.value()) {
    local->shape.clear();
    local->element_size = 0;
    local->buffer.reset();
  } else {
    const auto& value = *request.value();
    local->shape.clear();
    if (value.shape()) {
      for (int dim : *value.shape()) {
        local->shape.push_back(dim);
      }
    }
    local->element_size = value.element_size();
    // TODO(benvanik): copy buffer data.
  }

  ASSIGN_OR_RETURN(
      auto value_offs,
      SerializeBufferView(*local, /*include_buffer_contents=*/true, fbb));
  rpc::SetFiberLocalResponseBuilder response(*fbb);
  response.add_value(value_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::ListBreakpointsResponse>> DebugService::ListBreakpoints(
    const rpc::ListBreakpointsRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: ListBreakpoints()";
  std::vector<Offset<rpc::BreakpointDef>> breakpoint_offs;
  for (const auto& breakpoint : breakpoints_) {
    breakpoint_offs.push_back(rpc::BreakpointDef::Pack(*fbb, &breakpoint));
  }
  auto breakpoints_offs = fbb->CreateVector(breakpoint_offs);
  rpc::ListBreakpointsResponseBuilder response(*fbb);
  response.add_breakpoints(breakpoints_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::AddBreakpointResponse>> DebugService::AddBreakpoint(
    const rpc::AddBreakpointRequest& request, FlatBufferBuilder* fbb) {
  if (!request.breakpoint()) {
    return InvalidArgumentErrorBuilder(IREE_LOC) << "No breakpoint specified";
  }
  absl::MutexLock lock(&mutex_);
  int breakpoint_id = Instance::NextUniqueId();
  VLOG(1) << "RPC: AddBreakpoint(" << breakpoint_id << ")";

  RETURN_IF_ERROR(SuspendAllFibers());

  rpc::BreakpointDefT breakpoint;
  request.breakpoint()->UnPackTo(&breakpoint);
  breakpoint.breakpoint_id = breakpoint_id;
  switch (breakpoint.breakpoint_type) {
    case rpc::BreakpointType::BYTECODE_FUNCTION:
    case rpc::BreakpointType::NATIVE_FUNCTION:
      for (auto* context : contexts_) {
        auto module_or = context->LookupModule(breakpoint.module_name);
        if (!module_or.ok()) continue;
        auto* module = module_or.ValueOrDie();
        RETURN_IF_ERROR(
            RegisterFunctionBreakpoint(context, module, &breakpoint));
      }
      break;
    default:
      return UnimplementedErrorBuilder(IREE_LOC) << "Unhandled breakpoint type";
  }
  breakpoints_.push_back(std::move(breakpoint));

  RETURN_IF_ERROR(ResumeAllFibers());

  auto breakpoint_offs = rpc::BreakpointDef::Pack(*fbb, &breakpoints_.back());
  rpc::AddBreakpointResponseBuilder response(*fbb);
  response.add_breakpoint(breakpoint_offs);
  return response.Finish();
}

StatusOr<Offset<rpc::RemoveBreakpointResponse>> DebugService::RemoveBreakpoint(
    const rpc::RemoveBreakpointRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: RemoveBreakpoint(" << request.breakpoint_id() << ")";
  RETURN_IF_ERROR(SuspendAllFibers());

  bool found = false;
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    if (it->breakpoint_id == request.breakpoint_id()) {
      auto& breakpoint = *it;
      found = true;
      switch (breakpoint.breakpoint_type) {
        case rpc::BreakpointType::BYTECODE_FUNCTION:
        case rpc::BreakpointType::NATIVE_FUNCTION:
          RETURN_IF_ERROR(UnregisterFunctionBreakpoint(breakpoint));
          break;
        default:
          return UnimplementedErrorBuilder(IREE_LOC)
                 << "Unhandled breakpoint type";
      }
      breakpoints_.erase(it);
      break;
    }
  }

  RETURN_IF_ERROR(ResumeAllFibers());
  if (!found) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Breakpoint ID " << request.breakpoint_id() << " not found";
  }

  rpc::RemoveBreakpointResponseBuilder response(*fbb);
  return response.Finish();
}

Status DebugService::RegisterModuleBreakpoints(SequencerContext* context,
                                               Module* module) {
  for (auto& breakpoint : breakpoints_) {
    switch (breakpoint.breakpoint_type) {
      case rpc::BreakpointType::BYTECODE_FUNCTION:
        if (breakpoint.module_name == module->name()) {
          RETURN_IF_ERROR(
              RegisterFunctionBreakpoint(context, module, &breakpoint));
        }
        break;
      default:
        // Not relevant to modules.
        break;
    }
  }
  return OkStatus();
}

Status DebugService::RegisterFunctionBreakpoint(
    SequencerContext* context, Module* module,
    rpc::BreakpointDefT* breakpoint) {
  if (!breakpoint->function_name.empty()) {
    ASSIGN_OR_RETURN(breakpoint->function_ordinal,
                     module->function_table().LookupFunctionOrdinalByName(
                         breakpoint->function_name));
  }
  RETURN_IF_ERROR(module->mutable_function_table()->RegisterBreakpoint(
      breakpoint->function_ordinal, breakpoint->bytecode_offset,
      std::bind(&DebugService::OnFunctionBreakpointHit, this,
                breakpoint->breakpoint_id, std::placeholders::_1)));
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(session->OnBreakpointResolved(*breakpoint, context));
  }
  return OkStatus();
}

Status DebugService::UnregisterFunctionBreakpoint(
    const rpc::BreakpointDefT& breakpoint) {
  for (auto* context : contexts_) {
    auto module_or = context->LookupModule(breakpoint.module_name);
    if (!module_or.ok()) continue;
    auto* module = module_or.ValueOrDie();
    RETURN_IF_ERROR(module->mutable_function_table()->UnregisterBreakpoint(
        breakpoint.function_ordinal, breakpoint.bytecode_offset));
  }
  return OkStatus();
}

Status DebugService::OnFunctionBreakpointHit(int breakpoint_id,
                                             const vm::Stack& stack) {
  absl::ReleasableMutexLock lock(&mutex_);
  LOG(INFO) << "Breakpoint hit: " << breakpoint_id;
  FiberState* source_fiber_state = nullptr;
  for (auto* fiber_state : fiber_states_) {
    if (fiber_state->mutable_stack() == std::addressof(stack)) {
      source_fiber_state = fiber_state;
      break;
    }
  }
  if (!source_fiber_state) {
    return InternalErrorBuilder(IREE_LOC)
           << "Fiber state not found for stack - race?";
  }
  RETURN_IF_ERROR(UnreadyAllSessions());
  for (auto* session : sessions_) {
    RETURN_IF_ERROR(
        session->OnBreakpointHit(breakpoint_id, *source_fiber_state));
  }
  lock.Release();

  // TODO(benvanik): on-demand attach if desired?

  // Wait until all clients are ready.
  auto wait_status = WaitUntilAllSessionsReady();
  if (IsAborted(wait_status)) {
    // This means we lost all sessions. Just continue.
    VLOG(1) << "No sessions active; ignoring breakpoint and continuing";
    return OkStatus();
  }
  return wait_status;
}

StatusOr<Offset<rpc::StartProfilingResponse>> DebugService::StartProfiling(
    const rpc::StartProfilingRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: StartProfiling()";
  // TODO(benvanik): implement profiling.
  // ASSIGN_OR_RETURN(auto* context, GetContext(request.context_id()));
  // rpc::StartProfilingResponseBuilder response(*fbb);
  // return response.Finish();
  return UnimplementedErrorBuilder(IREE_LOC)
         << "StartProfiling not yet implemented";
}

StatusOr<Offset<rpc::StopProfilingResponse>> DebugService::StopProfiling(
    const rpc::StopProfilingRequest& request, FlatBufferBuilder* fbb) {
  absl::MutexLock lock(&mutex_);
  VLOG(1) << "RPC: StopProfiling()";
  // TODO(benvanik): implement profiling.
  // ASSIGN_OR_RETURN(auto* context, GetContext(request.context_id()));
  // rpc::StopProfilingResponseBuilder response(*fbb);
  // return response.Finish();
  return UnimplementedErrorBuilder(IREE_LOC)
         << "StopProfiling not yet implemented";
}

}  // namespace debug
}  // namespace vm
}  // namespace iree
