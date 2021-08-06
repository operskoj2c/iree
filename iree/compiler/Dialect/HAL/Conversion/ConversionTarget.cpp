// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Conversion/ConversionTarget.h"

#include "iree/compiler/Dialect/HAL/Conversion/TypeConverter.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/Utils/TypeUtils.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "iree/compiler/Dialect/Util/IR/UtilTypes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"

namespace mlir {
namespace iree_compiler {

HALConversionTarget::HALConversionTarget(MLIRContext *context,
                                         TypeConverter &typeConverter)
    : ConversionTarget(*context) {
  // The HAL dialect allows hal ops as input as we may be running on partially
  // processed files or may have already lowered some constructs (like constant
  // pools).
  addLegalDialect("hal");

  // We don't care about the contents of a HAL executable: it may have any kind
  // of dialect and type usage.
  addLegalOp<IREE::HAL::ExecutableOp>();
  markOpRecursivelyLegal<IREE::HAL::ExecutableOp>();

  // There are a variety of patterns which convert std.dim and std.rank ops
  // to corresponding HAL ops. All should be eliminated.
  addIllegalOp<memref::DimOp>();
  addIllegalOp<mlir::RankOp>();
  addIllegalOp<tensor::DimOp>();

  // Metadata ops are dynamically legal if their types are legal.
  addDynamicallyLegalOp<Shape::TieShapeOp>([&](Shape::TieShapeOp op) {
    return typeConverter.isLegal(op.result().getType());
  });

  // Setup the fallback handler such that all ops without explicitly
  // registered patterns will be checked to ensure that they don't use any
  // illegal types.
  markUnknownOpDynamicallyLegal([&](Operation *op) {
    // Short-circuit test that bails on the first illegal type.
    const auto isTypeIllegal = [&](Type type) {
      return !typeConverter.isLegal(type);
    };
    return !(llvm::any_of(op->getOperandTypes(), isTypeIllegal) ||
             llvm::any_of(op->getResultTypes(), isTypeIllegal));
  });
}

// static
LogicalResult HALConversionTarget::applyDefaultBufferRewrite(
    Operation *srcOp, ArrayRef<Value> operands, StringRef dstOpName,
    TypeConverter &typeConverter, ConversionPatternRewriter &rewriter) {
  OperationState state{srcOp->getLoc(), dstOpName};
  state.addAttributes(srcOp->getAttrs());

  for (auto srcDstOperand : llvm::zip(srcOp->getOperands(), operands)) {
    auto srcOperand = std::get<0>(srcDstOperand);
    auto dstOperand = std::get<1>(srcDstOperand);
    if (HALTypeConverter::shouldConvertToBuffer(srcOperand.getType())) {
      // Create the buffer view that we'll pass to the function.
      // Note that we expect this to be CSE'd if there are multiple calls
      // using the same buffer.
      auto operand = IREE::HAL::TensorRewriteAdaptor::getChecked(
          srcOp->getLoc(), srcOperand, dstOperand, rewriter);
      if (!operand.hasValue()) {
        return srcOp->emitOpError() << "unable to create adaptor for operand";
      }
      auto bufferView = operand->getBufferView();
      if (!bufferView) {
        return srcOp->emitOpError() << "unable to get buffer view for operand";
      }
      state.addOperands({bufferView});
    } else {
      // Normal pass-through operand.
      state.addOperands({dstOperand});
    }
  }
  for (auto resultType : srcOp->getResultTypes()) {
    if (HALTypeConverter::shouldConvertToBuffer(resultType)) {
      state.addTypes(IREE::HAL::BufferViewType::get(rewriter.getContext()));
    } else {
      // Normal pass-through result.
      if (failed(typeConverter.convertType(resultType, state.types))) {
        return failure();
      }
    }
  }

  auto *dstOp = rewriter.createOperation(state);

  // Now unpack any of the buffer views we may have returned.
  SmallVector<Value, 4> results;
  for (auto resultTypeValue :
       llvm::zip(srcOp->getResultTypes(), dstOp->getResults())) {
    Type resultType;
    Value resultValue;
    std::tie(resultType, resultValue) = resultTypeValue;
    if (HALTypeConverter::shouldConvertToBuffer(resultType)) {
      results.push_back(rewriter.createOrFold<IREE::HAL::BufferViewBufferOp>(
          srcOp->getLoc(), IREE::HAL::BufferType::get(rewriter.getContext()),
          resultValue));
    } else {
      results.push_back(resultValue);
    }
  }

  rewriter.replaceOp(srcOp, results);
  return success();
}

}  // namespace iree_compiler
}  // namespace mlir
