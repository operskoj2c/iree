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

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

// Creates two kind of benchmark functions:
//   - Creates exported functions to invoke each executable op.
//   - Clones each exported functions (including those just created) with
//     placeholder constant inputs instead of arguments and removes the
//     exported attribute from the old functions.
// The input are provided using flow.variable and flow.lookup.
class CreateBenchmarkFuncs
    : public PassWrapper<CreateBenchmarkFuncs, OperationPass<ModuleOp>> {
 public:
  void runOnOperation() override {
    ModuleOp moduleOp = getOperation();
    auto builder = OpBuilder::atBlockBegin(moduleOp.getBody());
    SymbolTable moduleSymbols(moduleOp);
    for (auto execOp : moduleOp.getOps<IREE::Flow::ExecutableOp>()) {
      for (auto& op : execOp.getBlock()) {
        auto dispatchEntryOp = dyn_cast<IREE::Flow::DispatchEntryOp>(op);
        if (!dispatchEntryOp) continue;
        auto execFuncOp = execOp.getInnerModule().lookupSymbol<FuncOp>(
            dispatchEntryOp.function_ref());
        Location loc = execFuncOp.getLoc();

        // Create a funcOp to invoke the dispatch function.
        std::string funcName = std::string(execFuncOp.getName()) + "_entry";
        auto funcType =
            builder.getFunctionType({}, execFuncOp.getType().getResults());
        auto funcOp = builder.create<FuncOp>(loc, funcName, funcType);
        funcOp.setAttr("iree.module.export", UnitAttr::get(&getContext()));
        Block* block = funcOp.addEntryBlock();

        // Build the body of the FuncOp.
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPoint(funcOp);
        auto blockBuilder = OpBuilder(block, block->begin());
        SmallVector<Value, 4> args;
        for (auto inputType : execFuncOp.getType().getInputs()) {
          args.push_back(getDummyInput(builder, blockBuilder, loc, inputType,
                                       moduleSymbols));
        }

        // TODO(hanchung): Use a real workload instead? We can probably
        // calculate the workload from the results.
        auto dummyWorkload = blockBuilder.create<ConstantIndexOp>(loc, 0);
        auto dispatchOp = blockBuilder.create<DispatchOp>(
            loc, dispatchEntryOp, dummyWorkload, funcType.getResults(), args);
        blockBuilder.create<mlir::ReturnOp>(loc, dispatchOp.getResults());
      }
    }

    // TODO(#3577): Move below part to a separate pass and use CallOp instead of
    // clone the region. The CallOp is materialized in an earlier stage. We
    // don't expect to see it at flow level.
    for (auto funcOp : moduleOp.getOps<FuncOp>()) {
      if (!funcOp.getAttr("iree.module.export")) {
        continue;
      }
      if (funcOp.getNumArguments() == 0) {
        continue;
      }

      Location loc = funcOp.getLoc();
      auto funcType =
          builder.getFunctionType({}, funcOp.getType().getResults());
      std::string funcName = std::string(funcOp.getName()) + "_dummy_args";
      auto newFuncOp = builder.create<FuncOp>(loc, funcName, funcType);
      newFuncOp.setAttr("iree.module.export", builder.getUnitAttr());
      Block* block = newFuncOp.addEntryBlock();

      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(newFuncOp);
      auto blockBuilder = OpBuilder::atBlockBegin(block);
      BlockAndValueMapping mapping;
      for (auto iter : llvm::enumerate(funcOp.getType().getInputs())) {
        auto arg = getDummyInput(builder, blockBuilder, loc, iter.value(),
                                 moduleSymbols);
        mapping.map(funcOp.getArgument(iter.index()), arg);
      }
      for (auto& op : funcOp.getRegion().begin()->getOperations()) {
        blockBuilder.clone(op, mapping);
      }

      funcOp.removeAttr("iree.module.export");
    }
  }

 private:
  Value getDummyInput(OpBuilder& moduleBuilder, OpBuilder& blockBuilder,
                      Location loc, Type inputType,
                      const SymbolTable& moduleSymbols) {
    std::string baseName = "_benchmark_input_";
    std::string name = baseName + std::to_string(uniqueId++);
    auto attr = blockBuilder.getZeroAttr(inputType);
    auto variableOp =
        moduleBuilder.create<VariableOp>(loc, name,
                                         /*isMutable=*/false, inputType, attr);
    SymbolTable::setSymbolVisibility(variableOp,
                                     SymbolTable::Visibility::Private);
    variableOp.setAttr("noinline", UnitAttr::get(moduleBuilder.getContext()));
    auto lookupOp = blockBuilder.create<IREE::Flow::VariableLoadOp>(
        loc, inputType, variableOp.getName());
    return lookupOp.getResult();
  }

  int uniqueId = 0;
};

std::unique_ptr<OperationPass<ModuleOp>> createCreateBenchmarkFuncs() {
  return std::make_unique<CreateBenchmarkFuncs>();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
