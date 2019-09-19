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

//===- XLAIndexPropagation.h -----------------------------------*- C++//-*-===//
//
// For an IREE dispatch function in XLA-HLO dialect, compute the indices of all
// tensors needed to produce the value of the result tensors at a particlar
// index.
//
//===----------------------------------------------------------------------===//
#ifndef IREE_COMPILER_TRANSLATION_SPIRV_XLAINDEXPROPOGATION_H
#define IREE_COMPILER_TRANSLATION_SPIRV_XLAINDEXPROPOGATION_H

#include "iree/compiler/Translation/SPIRV/IndexComputation.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Dialect/StandardOps/Ops.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Function.h"
#include "third_party/tensorflow/compiler/mlir/xla/ir/hlo_ops.h"

namespace mlir {
namespace iree_compiler {

class XLABroadcastInDimOpIndexPropagation final
    : public IndexPropagationOp<xla_hlo::BroadcastInDimOp> {
 public:
  using IndexPropagationOp<xla_hlo::BroadcastInDimOp>::IndexPropagationOp;

  LogicalResult propagateIndexMap(
      Operation *operation, AffineMap resultIndex,
      SmallVectorImpl<AffineMap> &operandIndices) const override;
};

/// For return ops, it is assumed that each thread is computing the value of one
/// element of the returned tensor.
template <typename OpTy>
class ReturnOpIndexPropagation : public IndexPropagationOp<OpTy> {
 public:
  using IndexPropagationOp<OpTy>::IndexPropagationOp;

  LogicalResult propagateIndexMap(
      Operation *operation, IndexComputationCache &indexMap) const override {
    if (operation->getNumOperands() != 1) {
      return operation->emitError("unhandled multiple return values");
    }
    auto returnValue = operation->getOperand(0);
    auto returnType = returnValue->getType().cast<RankedTensorType>();
    auto returnRank = returnType.getRank();
    if (returnRank > 3) {
      return operation->emitError("unhandled return tensor of dimension ")
             << returnType.getShape().size();
    }
    // Have as many symbols as the rank of the input tensor. These symbols map
    // to GlobalInvocationID along the three dimensions.
    Builder builder(operation->getContext());
    SmallVector<AffineExpr, 4> affineExprs;
    for (size_t i = returnRank; i > 0; --i) {
      affineExprs.push_back(builder.getAffineDimExpr(i - 1));
    }
    indexMap[operation->getOperand(0)]
            [builder.getAffineMap(returnRank, 0, affineExprs)];
    return success();
  }
};

/// Index propogation for XLA Transpose.
class XLATransposeOpIndexPropagation final
    : public TransposeOpIndexPropagation<xla_hlo::TransposeOp> {
 public:
  using TransposeOpIndexPropagation<
      xla_hlo::TransposeOp>::TransposeOpIndexPropagation;
  LogicalResult propagateIndexMap(
      Operation *op, AffineMap resultIndex,
      SmallVectorImpl<AffineMap> &indexMap) const override;
};

/// Utility function that does index propagation for a fn that has only XLA-HLO
/// ops.
inline LogicalResult propagateIndexForXLAFn(FuncOp fn,
                                            IndexComputationCache &map) {
  IndexPropagationList<NoBroadcastPwOpIndexPropagation<xla_hlo::AddOp>,
                       NoBroadcastPwOpIndexPropagation<xla_hlo::MulOp>,
                       ReturnOpIndexPropagation<ReturnOp>,
                       XLATransposeOpIndexPropagation>
      indexList;
  if (!fn.getOperation()->getNumRegions()) {
    return success();
  }
  return indexList.propagate(fn.getOperation()->getRegion(0), map);
}
}  // namespace iree_compiler
}  // namespace mlir
#endif  // IREE_COMPILER_TRANSLATION_SPIRV_XLAINDEXPROPOGATION_H
