// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Flow/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

namespace {

class StripSignednessPass : public StripSignednessBase<StripSignednessPass> {
 public:
  explicit StripSignednessPass() {}
  void runOnOperation() override;
};

class IntegerTypeConverter : public TypeConverter {
 public:
  static Type convertType(Type type) {
    if (auto iType = type.dyn_cast<IntegerType>()) {
      if (!iType.isSignless()) {
        return IntegerType::get(type.getContext(),
                                iType.getIntOrFloatBitWidth());
      }
    }
    return type;
  }
  static Type convertTensor(RankedTensorType type) {
    auto newType = RankedTensorType::get(type.getShape(),
                                         convertType(type.getElementType()));
    return newType;
  }
  explicit IntegerTypeConverter() {
    addConversion([](Type type) { return convertType(type); });
    addConversion(convertTensor);
  }
};

// Handles the type conversion component of the TypeConversion. This updates
// conversion patterns that used the original Quant types to be updated to
// the non-quant variants.
class GenericTypeConvert : public ConversionPattern {
 public:
  GenericTypeConvert(MLIRContext* context, TypeConverter& converter)
      : ConversionPattern(converter, MatchAnyOpTypeTag(), 0, context) {}
  LogicalResult matchAndRewrite(
      Operation* op, ArrayRef<Value> operands,
      ConversionPatternRewriter& rewriter) const override {
    llvm::SmallVector<Type, 4> newResults;
    if (isa<FuncOp>(op)) {
      return failure();
    }

    (void)getTypeConverter()->convertTypes(op->getResultTypes(), newResults);
    OperationState state(op->getLoc(), op->getName().getStringRef(), operands,
                         newResults, op->getAttrs(), op->getSuccessors());
    for (Region& r : op->getRegions()) {
      Region* newRegion = state.addRegion();
      rewriter.inlineRegionBefore(r, *newRegion, newRegion->begin());
      TypeConverter::SignatureConversion result(newRegion->getNumArguments());
      (void)getTypeConverter()->convertSignatureArgs(
          newRegion->getArgumentTypes(), result);
      rewriter.applySignatureConversion(newRegion, result);
    }
    Operation* newOp = rewriter.createOperation(state);
    rewriter.replaceOp(op, newOp->getResults());
    return success();
  }
};

static bool isIllegalType(Type type) {
  if (IntegerType ity = type.dyn_cast<IntegerType>()) return !ity.isSignless();
  if (auto shapedType = type.dyn_cast<ShapedType>()) {
    return isIllegalType(shapedType.getElementType());
  }
  return false;
}

void StripSignednessPass::runOnOperation() {
  IntegerTypeConverter converter;
  ConversionTarget target(getContext());

  // Operations are legal if they don't contain any illegal type.
  target.markUnknownOpDynamicallyLegal([](Operation* op) {
    if (auto funcOp = dyn_cast<FuncOp>(op)) {
      for (Type type : funcOp.getType().getInputs()) {
        if (isIllegalType(type)) return false;
      }
      for (Type type : funcOp.getType().getResults()) {
        if (isIllegalType(type)) return false;
      }
    }
    for (Type type : op->getResultTypes()) {
      if (type && isIllegalType(type)) return false;
    }
    for (Type type : op->getOperandTypes()) {
      if (type && isIllegalType(type)) return false;
    }
    return true;
  });

  auto* ctx = &getContext();
  auto func = getOperation();

  RewritePatternSet patterns(&getContext());
  patterns.insert<GenericTypeConvert>(ctx, converter);
  populateFunctionOpInterfaceTypeConversionPattern<FuncOp>(patterns, converter);

  if (failed(applyFullConversion(func, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

}  // namespace

std::unique_ptr<OperationPass<mlir::FuncOp>> createStripSignednessPass() {
  return std::make_unique<StripSignednessPass>();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
