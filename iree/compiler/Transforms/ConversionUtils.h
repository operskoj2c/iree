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

#ifndef IREE_COMPILER_TRANSFORMS_CONVERSIONUTILS_H_
#define IREE_COMPILER_TRANSFORMS_CONVERSIONUTILS_H_

#include "iree/compiler/Utils/MemRefUtils.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

template <typename SrcOp, typename DstOp>
struct UnaryOpLowering : public ConversionPattern {
  explicit UnaryOpLowering(MLIRContext *context)
      : ConversionPattern(SrcOp::getOperationName(), 1, context) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto *value = loadAccessValue(op->getLoc(), operands[0], rewriter);
    value = wrapAsMemRef(value, op, rewriter);

    auto dstType = getMemRefType(op->getResult(0), rewriter);
    auto dstOp = rewriter.create<DstOp>(op->getLoc(), dstType, value);
    auto result = dstOp.getResult();
    result = wrapAsTensor(result, op, rewriter);

    rewriter.replaceOp(
        op, {loadResultValue(op->getLoc(), op->getResult(0)->getType(), result,
                             rewriter)});
    return matchSuccess();
  }
};

template <typename SrcOp, typename DstOp>
struct BinaryOpLowering : public ConversionPattern {
  explicit BinaryOpLowering(MLIRContext *context)
      : ConversionPattern(SrcOp::getOperationName(), 1, context) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto *lhsValue = loadAccessValue(op->getLoc(), operands[0], rewriter);
    auto *rhsValue = loadAccessValue(op->getLoc(), operands[1], rewriter);
    auto dstType = getMemRefType(op->getResult(0), rewriter);

    lhsValue = wrapAsMemRef(lhsValue, op, rewriter);
    rhsValue = wrapAsMemRef(rhsValue, op, rewriter);

    auto midOp =
        rewriter.create<DstOp>(op->getLoc(), dstType, lhsValue, rhsValue);
    auto result = midOp.getResult();
    result = wrapAsTensor(result, op, rewriter);

    rewriter.replaceOp(
        op, {loadResultValue(op->getLoc(), op->getResult(0)->getType(), result,
                             rewriter)});
    return matchSuccess();
  }
};

template <typename SrcOp, typename DstOp>
struct TernaryOpLowering : public ConversionPattern {
  explicit TernaryOpLowering(MLIRContext *context)
      : ConversionPattern(SrcOp::getOperationName(), 1, context) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto *aValue = loadAccessValue(op->getLoc(), operands[0], rewriter);
    auto *bValue = loadAccessValue(op->getLoc(), operands[1], rewriter);
    auto *cValue = loadAccessValue(op->getLoc(), operands[2], rewriter);

    aValue = wrapAsMemRef(aValue, op, rewriter);
    bValue = wrapAsMemRef(bValue, op, rewriter);
    cValue = wrapAsMemRef(cValue, op, rewriter);

    auto dstType = getMemRefType(op->getResult(0), rewriter);
    auto dstOp =
        rewriter.create<DstOp>(op->getLoc(), dstType, aValue, bValue, cValue);
    auto result = dstOp.getResult();
    result = wrapAsTensor(result, op, rewriter);

    rewriter.replaceOp(
        op, {loadResultValue(op->getLoc(), op->getResult(0)->getType(), result,
                             rewriter)});
    return matchSuccess();
  }
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_TRANSFORMS_CONVERSIONUTILS_H_
