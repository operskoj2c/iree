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

#include <utility>

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

static SmallVector<Value, 4> filterTensorValues(ValueRange&& range) {
  SmallVector<Value, 4> result;
  for (auto value : range) {
    if (value.getType().isa<TensorType>()) result.push_back(value);
  }
  return result;
}

class InjectDispatchTracingPass
    : public PassWrapper<InjectDispatchTracingPass, OperationPass<FuncOp>> {
 public:
  InjectDispatchTracingPass() = default;

  void runOnOperation() override {
    for (auto dispatchOp : getOperation().getOps<Dispatch2Op>()) {
      std::string entryPointName =
          dispatchOp.entry_point().getRootReference().str();
      for (FlatSymbolRefAttr nestedRef :
           dispatchOp.entry_point().getNestedReferences()) {
        entryPointName = (entryPointName + "::" + nestedRef.getValue()).str();
      }

      // Input tensors:
      OpBuilder builder(dispatchOp);
      builder.create<TensorTraceOp>(
          dispatchOp.getLoc(), filterTensorValues(dispatchOp.operands()),
          builder.getStringAttr(entryPointName + " inputs"));

      // Output tensors:
      builder.setInsertionPointAfter(dispatchOp);
      builder.create<TensorTraceOp>(
          dispatchOp.getLoc(), filterTensorValues(dispatchOp.results()),
          builder.getStringAttr(entryPointName + " outputs"));
    }
  }
};

std::unique_ptr<OperationPass<FuncOp>> createInjectDispatchTracingPass() {
  return std::make_unique<InjectDispatchTracingPass>();
}

static PassRegistration<InjectDispatchTracingPass> pass(
    "iree-flow-inject-dispatch-tracing",
    "Outlines dispatch regions into executables");

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
