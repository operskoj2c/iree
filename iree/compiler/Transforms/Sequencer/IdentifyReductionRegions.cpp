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

#include <algorithm>

#include "iree/compiler/IR/Ops.h"
#include "iree/compiler/IR/Types.h"
#include "iree/compiler/Utils/DispatchUtils.h"
#include "third_party/llvm/llvm/include/llvm/ADT/ArrayRef.h"
#include "third_party/llvm/llvm/include/llvm/ADT/DenseMap.h"
#include "third_party/llvm/llvm/include/llvm/ADT/DenseSet.h"
#include "third_party/llvm/llvm/include/llvm/ADT/STLExtras.h"
#include "third_party/llvm/llvm/include/llvm/ADT/SetVector.h"
#include "third_party/llvm/llvm/include/llvm/ADT/SmallVector.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Dialect/StandardOps/Ops.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Attributes.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/BlockAndValueMapping.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Builders.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Location.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/MLIRContext.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/StandardTypes.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Pass/Pass.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Pass/PassRegistry.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Support/LLVM.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Support/LogicalResult.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Transforms/Utils.h"
#include "tensorflow/compiler/mlir/xla/ir/hlo_ops.h"

namespace mlir {
namespace iree_compiler {

namespace {

// Builds a new iree.reduction_region with the given |invocationRegion|.
// The new region will be inserted after |originalOp|.
//
// All |invocationRegion| ops must be compatible with the |workload| specified
// as they will all be dispatched with the same workgroup structure. The
// |invocationRegion| will not be modified.
LogicalResult buildReductionRegion(Operation *originalOp,
                                   ArrayRef<Value *> operands,
                                   ArrayRef<Value *> initialValues,
                                   ArrayRef<int64_t> dimensions,
                                   Region &invocationRegion) {
  OpBuilder parentBuilder(originalOp);

  // Compute the workload based on the output shape.
  // When variadic all output shapes match so we can just take the first.
  auto *workload = calculateWorkload(originalOp, originalOp->getResult(0));

  // Build the region op and add it to the parent block.
  SmallVector<Type, 4> resultTypes{originalOp->getResultTypes()};
  auto reductionRegionOp = parentBuilder.create<IREE::ReductionRegionOp>(
      originalOp->getLoc(), resultTypes, workload, operands, initialValues,
      dimensions);

  // Create the block and setup the arg mapping for captured values.
  BlockAndValueMapping mapping;
  invocationRegion.cloneInto(&reductionRegionOp.getBody(), mapping);

  // Replace xla_hlo.return -> iree.return.
  OpBuilder regionBuilder(reductionRegionOp.getBody());
  reductionRegionOp.walk([&](xla_hlo::ReturnOp returnOp) {
    regionBuilder.setInsertionPoint(returnOp);
    SmallVector<Value *, 4> returnValues(returnOp.getOperands());
    regionBuilder.create<IREE::ReturnOp>(returnOp.getLoc(), returnValues);
    returnOp.erase();
  });

  // Replace usage of values with the results of the region.
  for (int i = 0; i < originalOp->getNumResults(); ++i) {
    originalOp->getResult(i)->replaceAllUsesWith(
        reductionRegionOp.getResult(i));
  }

  return success();
}

// Converts an xla_hlo::ReduceOp to a reduction region and inlines the target
// computation into the region body.
LogicalResult buildReductionRegionFromXLAReduceOp(xla_hlo::ReduceOp reduceOp) {
  SmallVector<Value *, 4> operands(reduceOp.getOperands());
  OperandAdaptor<xla_hlo::ReduceOp> adaptor(operands);

  SmallVector<int64_t, 4> dimensions;
  for (auto dim : reduceOp.dimensions().getIntValues()) {
    dimensions.push_back(dim.getSExtValue());
  }

  // Create the iree.reduction_region.
  if (failed(buildReductionRegion(reduceOp, adaptor.operands(),
                                  adaptor.init_values(), dimensions,
                                  reduceOp.body()))) {
    return failure();
  }

  // Remove original XLA reduction op.
  reduceOp.erase();

  return success();
}

// Identifies reduction ops and moves them into reduction regions.
LogicalResult identifyBlockReductionRegions(FuncOp funcOp, Block *block) {
  // Fixed point iteration until we can no longer fuse anything.
  bool didFindAnyNewRegions;
  do {
    // Iterate in reverse so we root further along in the op list.
    didFindAnyNewRegions = false;
    for (auto &rootOp : llvm::reverse(*block)) {
      if (auto reduceOp = dyn_cast<xla_hlo::ReduceOp>(rootOp)) {
        if (failed(buildReductionRegionFromXLAReduceOp(reduceOp))) {
          return failure();
        }

        // Successfully created a dispatch region from the ops and we must now
        // start over again as we've likely trashed the whole block structure.
        didFindAnyNewRegions = true;
        break;
      }
    }
  } while (didFindAnyNewRegions);
  return success();
}

}  // namespace

// Identifies reduction ops and moves their targets into iree.reduction_regions.
class IdentifyReductionRegionsPass
    : public ModulePass<IdentifyReductionRegionsPass> {
 public:
  void runOnModule() override {
    for (auto funcOp : getModule().getOps<FuncOp>()) {
      for (auto &block : funcOp) {
        if (failed(identifyBlockReductionRegions(funcOp, &block))) {
          return signalPassFailure();
        }
      }
    }
  }
};

std::unique_ptr<OpPassBase<ModuleOp>> createIdentifyReductionRegionsPass() {
  return std::make_unique<IdentifyReductionRegionsPass>();  // NOLINT
}

static PassRegistration<IdentifyReductionRegionsPass> pass(
    "iree-identify-reduction-regions",
    "Identifies reduction regions based on input reduction ops.");

}  // namespace iree_compiler
}  // namespace mlir
