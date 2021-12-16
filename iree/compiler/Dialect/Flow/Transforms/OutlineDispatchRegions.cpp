// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <utility>

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/Flow/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "llvm/Support/Debug.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

#define DEBUG_TYPE "iree-dispatch"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {
namespace {

// Creates a flow.executable out of a set of functions, pulling in all other
// functions reachable by the provided functions.
static ExecutableOp createExecutable(Location loc, StringRef executableName,
                                     ArrayRef<mlir::FuncOp> funcOps,
                                     ModuleOp parentModuleOp) {
  assert(!funcOps.empty() && "must have at least one entry function");

  // Create the executable that will contain the outlined region.
  // NOTE: this will get uniquified if we have multiple in the same block.
  OpBuilder parentModuleBuilder(&parentModuleOp.getBody()->back());
  auto executableOp =
      parentModuleBuilder.create<IREE::Flow::ExecutableOp>(loc, executableName);

  // Create the inner ModuleOp that contains the original functions. We need
  // to provide this shim as some ops (like std.call) look for the
  // containing module to provide symbol resolution.
  OpBuilder executableBuilder(executableOp);
  executableBuilder.setInsertionPointToStart(&executableOp.getBlock());
  auto innerModule = executableBuilder.create<mlir::ModuleOp>(loc);
  for (auto funcOp : funcOps) {
    innerModule.push_back(funcOp);
  }

  // Copy all reachable functions into the executable.
  // Linker passes may dedupe these later on.
  OpBuilder innerModuleBuilder = OpBuilder::atBlockEnd(innerModule.getBody());
  innerModuleBuilder.setInsertionPoint(innerModule.getBody(),
                                       ++innerModule.getBody()->begin());

  return executableOp;
}

// Converts a dispatch region op into a dispatch op to the outlined region.
static LogicalResult convertToDispatchOp(DispatchWorkgroupsOp regionOp,
                                         ExecutableOp executableOp,
                                         DispatchEntryOp entryPointOp) {
  // Insert at the same place as the original region.
  OpBuilder builder(regionOp);

  // Create the dispatch op to the executable function.
  // Note that we copy the tied operand indices from the workgroups op - it
  // lines up 1:1 with the dispatch once we've outlined things.
  auto dispatchOp = builder.create<DispatchOp>(
      regionOp.getLoc(), entryPointOp, regionOp.workgroup_count(),
      regionOp.getResultTypes(), regionOp.result_dims(), regionOp.operands(),
      regionOp.operand_dims(), regionOp.tied_operandsAttr());

  // Replace uses of the existing results with the new results.
  for (int i = 0; i < regionOp.getNumResults(); ++i) {
    regionOp.getResult(i).replaceAllUsesWith(dispatchOp.getResult(i));
  }

  // Erase original region.
  regionOp.erase();

  return success();
}

// Converts a dispatch region body to a free-floating function.
static mlir::FuncOp createWorkgroupFunc(Location loc, StringRef functionName,
                                        Region &region) {
  // Build function type matching the region signature.
  auto functionType = FunctionType::get(
      region.getContext(), region.getArgumentTypes(), /*results=*/{});

  // Clone region into the function body.
  auto funcOp = mlir::FuncOp::create(loc, functionName, functionType);
  BlockAndValueMapping mapping;
  region.cloneInto(&funcOp.getBody(), mapping);

  // Replace flow.return with std.return.
  // NOTE: in the dispatch workgroups case the return should have no values.
  for (auto &block : funcOp.getBlocks()) {
    if (auto returnOp = dyn_cast<IREE::Flow::ReturnOp>(block.back())) {
      OpBuilder builder(returnOp);
      builder.create<mlir::ReturnOp>(
          returnOp.getLoc(), llvm::to_vector<4>(returnOp.getOperands()));
      returnOp.erase();
    }
  }

  return funcOp;
}

// Outlines a dispatch region into a flow.executable and replaces the region op
// with a dispatch to that outlined executable.
static LogicalResult outlineDispatchWorkgroupsOp(
    std::string namePrefix, DispatchWorkgroupsOp regionOp) {
  // Convert the region to a free-floating function.
  auto workgroupFuncOp =
      createWorkgroupFunc(regionOp.getLoc(), namePrefix, regionOp.body());
  if (!workgroupFuncOp) {
    return failure();
  }

  // Create the executable with the region cloned into it.
  Operation *parentFuncOp =
      regionOp->getParentWithTrait<OpTrait::FunctionLike>();
  auto executableOp =
      createExecutable(regionOp.getLoc(), namePrefix, {workgroupFuncOp},
                       parentFuncOp->getParentOfType<mlir::ModuleOp>());
  executableOp.getOperation()->moveBefore(parentFuncOp);
  executableOp.setPrivate();

  // Add executable entry point pointing at the function.
  OpBuilder builder(executableOp.body());
  auto entryPointOp = builder.create<DispatchEntryOp>(
      regionOp.getLoc(), workgroupFuncOp.getName(),
      SymbolRefAttr::get(workgroupFuncOp),
      builder.getIndexAttr(regionOp.getWorkgroupRank()));

  // Finally convert the dispatch region into a dispatch to the outlined func.
  return convertToDispatchOp(regionOp, executableOp, entryPointOp);
}

}  // namespace

class OutlineDispatchRegionsPass
    : public OutlineDispatchRegionsBase<OutlineDispatchRegionsPass> {
 public:
  OutlineDispatchRegionsPass() = default;

  void runOnOperation() override {
    // Convert each dispatch region into a flow.executable + dispatch op.
    int initializerCount = 0;
    for (auto it : llvm::enumerate(getOperation().getOps())) {
      Operation &op = it.value();
      if (!op.hasTrait<OpTrait::FunctionLike>()) continue;

      // Generate a nice name if possible.
      std::string opName;
      if (auto funcOp = llvm::dyn_cast<mlir::FuncOp>(op)) {
        opName = funcOp.getName().str();
      } else if (llvm::isa<IREE::Util::InitializerOp>(op)) {
        opName =
            std::string("_initializer_") + std::to_string(initializerCount++);
      } else {
        opName = std::string("_function_like_") + std::to_string(it.index());
      }

      auto &bodyRegion = function_like_impl::getFunctionBody(&op);
      // Outline all of the dispatch regions ops in this function.
      auto dispatchWorkgroupsOps =
          llvm::to_vector<8>(bodyRegion.getOps<DispatchWorkgroupsOp>());
      for (int i = 0; i < dispatchWorkgroupsOps.size(); ++i) {
        std::string namePrefix = (opName + "_dispatch_" + llvm::Twine(i)).str();
        if (failed(outlineDispatchWorkgroupsOp(namePrefix,
                                               dispatchWorkgroupsOps[i]))) {
          return signalPassFailure();
        }
      }
    }
  }
};

std::unique_ptr<OperationPass<mlir::ModuleOp>>
createOutlineDispatchRegionsPass() {
  return std::make_unique<OutlineDispatchRegionsPass>();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
