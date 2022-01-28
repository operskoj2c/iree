// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/IR/HALTypes.h"
#include "llvm/ADT/ArrayRef.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace {

struct BufferViewDimPattern : public OpConversionPattern<tensor::DimOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      tensor::DimOp dimOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!adaptor.source().getType().isa<IREE::HAL::BufferViewType>()) {
      return failure();
    }
    Optional<int64_t> index = dimOp.getConstantIndex();
    assert(index.hasValue() && "expect constant index in `std.dim` operation");
    rewriter.replaceOpWithNewOp<IREE::HAL::BufferViewDimOp>(
        dimOp, dimOp.getResult().getType(), adaptor.source(),
        rewriter.getIndexAttr(index.getValue()));
    return success();
  }
};

struct BufferViewRankPattern : public OpConversionPattern<tensor::RankOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      tensor::RankOp rankOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!adaptor.tensor().getType().isa<IREE::HAL::BufferViewType>()) {
      return failure();
    }
    rewriter.replaceOpWithNewOp<IREE::HAL::BufferViewRankOp>(
        rankOp, rankOp.getResult().getType(), adaptor.tensor());
    return success();
  }
};

}  // namespace

void populateStandardShapeToHALPatterns(MLIRContext *context,
                                        ConversionTarget &conversionTarget,
                                        RewritePatternSet &patterns,
                                        TypeConverter &typeConverter) {
  // Ensure all shape related ops are fully converted as we should no longer
  // have any types they are valid to be used on after this conversion.
  conversionTarget.addIllegalOp<tensor::DimOp, tensor::RankOp>();

  patterns.insert<BufferViewDimPattern, BufferViewRankPattern>(context);
}

}  // namespace iree_compiler
}  // namespace mlir
