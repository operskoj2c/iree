// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/InputConversion/MHLO/PassDetail.h"
#include "iree/compiler/InputConversion/MHLO/Passes.h"
#include "iree/compiler/InputConversion/MHLO/Rewriters.h"
#include "mlir-hlo/Dialect/mhlo/IR/chlo_ops.h"
#include "mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace MHLO {

namespace {

bool isComplexTensor(Value v) {
  if (auto tt = v.getType().dyn_cast<TensorType>()) {
    return tt.getElementType().isa<ComplexType>();
  }
  return false;
}

Type convertComplexTensorTypeToReal(Type complexTensorType) {
  auto newElementType = complexTensorType.cast<TensorType>()
                            .getElementType()
                            .cast<ComplexType>()
                            .getElementType();
  if (auto tt = complexTensorType.dyn_cast<RankedTensorType>()) {
    return RankedTensorType::get(tt.getShape(), newElementType,
                                 tt.getEncoding());
  } else if (auto tt = complexTensorType.dyn_cast<UnrankedTensorType>()) {
    return UnrankedTensorType::get(newElementType);
  }
  llvm_unreachable("unknown TensorType subclass");
}

// Add and subtraction are elementwise and can be distributed across the real
// and imaginary components.
template <typename OpTy>
struct ConvertAddSubOp : public OpConversionPattern<OpTy> {
  using OpConversionPattern<OpTy>::OpConversionPattern;

  static Value createOp(OpBuilder &b, mhlo::AddOp op, Value lhs, Value rhs) {
    return b.create<mhlo::AddOp>(op.getLoc(), lhs, rhs);
  }
  static Value createOp(OpBuilder &b, mhlo::SubOp op, Value lhs, Value rhs) {
    return b.create<mhlo::SubOp>(op.getLoc(), lhs, rhs);
  }
  static Value createOp(OpBuilder &b, chlo::BroadcastAddOp op, Value lhs,
                        Value rhs) {
    return b.create<chlo::BroadcastAddOp>(op.getLoc(), lhs, rhs, nullptr);
  }
  static Value createOp(OpBuilder &b, chlo::BroadcastSubOp op, Value lhs,
                        Value rhs) {
    return b.create<chlo::BroadcastSubOp>(op.getLoc(), lhs, rhs, nullptr);
  }

  LogicalResult matchAndRewrite(
      OpTy op, typename OpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    if (!isComplexTensor(adaptor.lhs()) || !isComplexTensor(adaptor.rhs())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }

    Value real = createOp(
        rewriter, op, rewriter.createOrFold<mhlo::RealOp>(loc, adaptor.lhs()),
        rewriter.createOrFold<mhlo::RealOp>(loc, adaptor.rhs()));
    Value imag = createOp(
        rewriter, op, rewriter.createOrFold<mhlo::ImagOp>(loc, adaptor.lhs()),
        rewriter.createOrFold<mhlo::ImagOp>(loc, adaptor.rhs()));
    Value result = rewriter.create<mhlo::ComplexOp>(loc, real, imag);
    rewriter.replaceOp(op, result);
    return success();
  }
};

// Complex multiplication results in a cross product multiplication between the
// real and imaginary components such that:
//   result.real = lhs.real * rhs.real - lhs.imag * rhs.imag
//   result.imag = lhs.imag * rhs.real + lhs.real * rhs.imag
template <typename MulOpTy>
struct ConvertMulOp : public OpConversionPattern<MulOpTy> {
  using OpConversionPattern<MulOpTy>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      MulOpTy op, typename MulOpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();

    if (!isComplexTensor(adaptor.lhs()) || !isComplexTensor(adaptor.rhs())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }

    auto lhsReal = rewriter.createOrFold<mhlo::RealOp>(loc, adaptor.lhs());
    auto lhsImag = rewriter.createOrFold<mhlo::ImagOp>(loc, adaptor.lhs());
    auto rhsReal = rewriter.createOrFold<mhlo::RealOp>(loc, adaptor.rhs());
    auto rhsImag = rewriter.createOrFold<mhlo::ImagOp>(loc, adaptor.rhs());

    auto realComponent = rewriter.create<mhlo::SubOp>(
        loc,
        rewriter.create<chlo::BroadcastMulOp>(loc, lhsReal, rhsReal,
                                              /*broadcast_dimensions=*/nullptr),
        rewriter.create<chlo::BroadcastMulOp>(
            loc, lhsImag, rhsImag, /*broadcast_dimensions=*/nullptr));
    auto imagComponent = rewriter.create<mhlo::AddOp>(
        loc,
        rewriter.create<chlo::BroadcastMulOp>(loc, lhsReal, rhsImag,
                                              /*broadcast_dimensions=*/nullptr),
        rewriter.create<chlo::BroadcastMulOp>(
            loc, lhsImag, rhsReal, /*broadcast_dimensions=*/nullptr));
    Value result = rewriter.createOrFold<mhlo::ComplexOp>(loc, realComponent,
                                                          imagComponent);
    rewriter.replaceOp(op, result);
    return success();
  }
};

// Division is performed by normalizing the denominator by multiplying by the
// conjugate of the rhs.
//   numerator = lhs * conj(rhs)
//   denominator = rhs * conj(rhs)
template <typename DivOpTy>
struct ConvertDivOp : public OpConversionPattern<DivOpTy> {
  using OpConversionPattern<DivOpTy>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      DivOpTy op, typename DivOpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();

    if (!isComplexTensor(adaptor.lhs()) || !isComplexTensor(adaptor.rhs())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }

    auto lhs = adaptor.lhs();
    auto rhs = adaptor.rhs();
    auto rhsReal = rewriter.createOrFold<mhlo::RealOp>(loc, rhs);
    auto rhsImag = rewriter.createOrFold<mhlo::ImagOp>(loc, rhs);

    Value conj = rewriter.createOrFold<mhlo::ComplexOp>(
        loc, rhsReal, rewriter.create<mhlo::NegOp>(loc, rhsImag));
    Value complexNumerator = rewriter.create<chlo::BroadcastMulOp>(
        loc, lhs, conj, /*broadcast_dimensions=*/nullptr);
    Value denominator = rewriter.create<mhlo::AddOp>(
        loc, rewriter.create<mhlo::MulOp>(loc, rhsReal, rhsReal),
        rewriter.create<mhlo::MulOp>(loc, rhsImag, rhsImag));

    Value realComponent = rewriter.create<chlo::BroadcastDivOp>(
        loc, rewriter.create<mhlo::RealOp>(loc, complexNumerator), denominator,
        /*broadcast_dimensions=*/nullptr);
    Value imagComponent = rewriter.create<chlo::BroadcastDivOp>(
        loc, rewriter.create<mhlo::ImagOp>(loc, complexNumerator), denominator,
        /*broadcast_dimensions=*/nullptr);

    Value result = rewriter.createOrFold<mhlo::ComplexOp>(loc, realComponent,
                                                          imagComponent);
    rewriter.replaceOp(op, result);
    return success();
  }
};

// Absolute value is evaluated as:
//   result = sqrt(val.real * val.real + val.imag * val.imag)
struct ConvertAbsOp : public OpConversionPattern<mhlo::AbsOp> {
  using OpConversionPattern<mhlo::AbsOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mhlo::AbsOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();

    if (!isComplexTensor(adaptor.operand())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }

    auto operandReal =
        rewriter.createOrFold<mhlo::RealOp>(loc, adaptor.operand());
    auto operandImag =
        rewriter.createOrFold<mhlo::ImagOp>(loc, adaptor.operand());
    rewriter.replaceOpWithNewOp<mhlo::SqrtOp>(
        op,
        rewriter.create<mhlo::AddOp>(
            loc, rewriter.create<mhlo::MulOp>(loc, operandReal, operandReal),
            rewriter.create<mhlo::MulOp>(loc, operandImag, operandImag)));
    return success();
  }
};

// Exponential can be lowered to an exponential on the real component and a
// sum of sinusoids of the imaginary component, which equates to a normal
// exponential operator multiplied by Euler's formula.
//
// Exp(a + ib) = Exp(a) * Exp(ib) = Exp(a) * Cos(b) + Exp(a) * iSin(b))
struct ConvertExpOp : public OpConversionPattern<mhlo::ExpOp> {
  using OpConversionPattern<mhlo::ExpOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mhlo::ExpOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();

    if (!isComplexTensor(adaptor.operand())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }

    auto operandReal = rewriter.create<mhlo::RealOp>(loc, adaptor.operand());
    auto operandImag = rewriter.create<mhlo::ImagOp>(loc, adaptor.operand());

    Value expReal = rewriter.create<mhlo::ExpOp>(loc, operandReal);
    Value result = rewriter.createOrFold<mhlo::ComplexOp>(
        loc,
        rewriter.create<mhlo::MulOp>(
            loc, rewriter.create<mhlo::CosOp>(loc, operandImag), expReal),
        rewriter.create<mhlo::MulOp>(
            loc, rewriter.create<mhlo::SinOp>(loc, operandImag), expReal));
    rewriter.replaceOp(op, result);
    return success();
  }
};

template <typename CompareOpTy, typename ComparatorOpTy>
struct ConvertCompareOp : public OpConversionPattern<CompareOpTy> {
  using OpConversionPattern<CompareOpTy>::OpConversionPattern;
  ConvertCompareOp(TypeConverter &typeConverter, MLIRContext *context,
                   mhlo::ComparisonDirection direction)
      : OpConversionPattern<CompareOpTy>(typeConverter, context),
        direction(mhlo::stringifyEnum(direction)) {}

  LogicalResult matchAndRewrite(
      CompareOpTy op, typename CompareOpTy::Adaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    Location loc = op.getLoc();

    if (!isComplexTensor(adaptor.lhs()) || !isComplexTensor(adaptor.rhs())) {
      return rewriter.notifyMatchFailure(op, "not complex tensor");
    }
    if (direction != op.comparison_direction()) {
      return rewriter.notifyMatchFailure(op, "not matching direction");
    }

    auto lhs = adaptor.lhs();
    auto rhs = adaptor.rhs();
    auto lhsReal = rewriter.createOrFold<mhlo::RealOp>(loc, lhs);
    auto lhsImag = rewriter.createOrFold<mhlo::ImagOp>(loc, lhs);
    auto rhsReal = rewriter.createOrFold<mhlo::RealOp>(loc, rhs);
    auto rhsImag = rewriter.createOrFold<mhlo::ImagOp>(loc, rhs);

    rewriter.replaceOpWithNewOp<ComparatorOpTy>(
        op,
        rewriter.create<chlo::BroadcastCompareOp>(
            loc, lhsReal, rhsReal,
            /*broadcast_dimensions=*/nullptr, op.comparison_directionAttr(),
            op.compare_typeAttr()),
        rewriter.create<chlo::BroadcastCompareOp>(
            loc, lhsImag, rhsImag,
            /*broadcast_dimensions=*/nullptr, op.comparison_directionAttr(),
            op.compare_typeAttr()));

    return success();
  }

  StringRef direction;
};

struct ElideComplexPattern : public OpConversionPattern<mhlo::ComplexOp> {
  using OpConversionPattern<mhlo::ComplexOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mhlo::ComplexOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.eraseOp(op);
    return success();
  }
};

struct ElideRealPattern : public OpConversionPattern<mhlo::RealOp> {
  using OpConversionPattern<mhlo::RealOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mhlo::RealOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto complexProducer =
        adaptor.getOperands()[0].getDefiningOp<mhlo::ComplexOp>();
    if (complexProducer) {
      rewriter.replaceOp(op, complexProducer.lhs());
      return success();
    }
    return failure();
  }
};

struct ElideImagPattern : public OpConversionPattern<mhlo::ImagOp> {
  using OpConversionPattern<mhlo::ImagOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mhlo::ImagOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    auto complexProducer =
        adaptor.getOperands()[0].getDefiningOp<mhlo::ComplexOp>();
    if (complexProducer) {
      rewriter.replaceOp(op, complexProducer.rhs());
      return success();
    }
    return failure();
  }
};

}  // namespace

void populateMHLOComplexToRealPatterns(MLIRContext *context,
                                       TypeConverter &typeConverter,
                                       RewritePatternSet &patterns) {
  // Add an subtract patterns.
  patterns.insert<ConvertAddSubOp<mhlo::AddOp>>(typeConverter, context);
  patterns.insert<ConvertAddSubOp<mhlo::SubOp>>(typeConverter, context);
  patterns.insert<ConvertAddSubOp<chlo::BroadcastAddOp>>(typeConverter,
                                                         context);
  patterns.insert<ConvertAddSubOp<chlo::BroadcastSubOp>>(typeConverter,
                                                         context);

  // Mul patterns.
  patterns.insert<ConvertMulOp<mhlo::MulOp>>(typeConverter, context);
  patterns.insert<ConvertMulOp<chlo::BroadcastMulOp>>(typeConverter, context);

  // Div patterns.
  patterns.insert<ConvertDivOp<mhlo::DivOp>>(typeConverter, context);
  patterns.insert<ConvertDivOp<chlo::BroadcastDivOp>>(typeConverter, context);

  // Unary ops.
  patterns.insert<ConvertAbsOp>(typeConverter, context);
  patterns.insert<ConvertExpOp>(typeConverter, context);

  // Compare ops.
  patterns.insert<ConvertCompareOp<mhlo::CompareOp, mhlo::OrOp>>(
      typeConverter, context, mhlo::ComparisonDirection::NE);
  patterns.insert<ConvertCompareOp<mhlo::CompareOp, mhlo::AndOp>>(
      typeConverter, context, mhlo::ComparisonDirection::EQ);
  patterns.insert<ConvertCompareOp<chlo::BroadcastCompareOp, mhlo::OrOp>>(
      typeConverter, context, mhlo::ComparisonDirection::NE);
  patterns.insert<ConvertCompareOp<chlo::BroadcastCompareOp, mhlo::AndOp>>(
      typeConverter, context, mhlo::ComparisonDirection::EQ);

  // Complex/Real/Imag conversions should fold away.
  // Note that this is an opinion taken because these patterns are targeted
  // at full conversion scenarios and we would rather know eagerly if
  // conversion is not possible. A more lax conversion would not include the
  // ElideComplexPattern.
  // Doing it this way makes error messages nice because a failure will report
  // which remaining live op is keeping it from being erased.
  patterns.insert<ElideComplexPattern>(typeConverter, context);
  patterns.insert<ElideRealPattern>(typeConverter, context);
  patterns.insert<ElideImagPattern>(typeConverter, context);
}

namespace {

struct TestMHLOConvertComplexToRealPass
    : public TestMHLOConvertComplexToRealBase<
          TestMHLOConvertComplexToRealPass> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<mhlo::MhloDialect, chlo::HloClientDialect>();
  }

  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    MLIRContext *context = &getContext();
    TypeConverter typeConverter;
    typeConverter.addConversion([](Type t) { return t; });

    populateMHLOComplexToRealPatterns(context, typeConverter, patterns);

    ConversionTarget target(*context);
    auto hasNoComplexTypes = [](Operation *op) {
      for (Value operand : op->getOperands()) {
        if (auto st = operand.getType().dyn_cast<ShapedType>()) {
          if (st.getElementType().isa<ComplexType>()) {
            return false;
          }
        }
      }
      for (Value result : op->getResults()) {
        if (auto st = result.getType().dyn_cast<ShapedType>()) {
          if (st.getElementType().isa<ComplexType>()) {
            return false;
          }
        }
      }
      return true;
    };

    target.addLegalDialect<mhlo::MhloDialect>();
    target.addLegalDialect<chlo::HloClientDialect>();
    target
        .addLegalDialect<StandardOpsDialect, mlir::arith::ArithmeticDialect>();

    // For the test, require that casts fully convert.
    target.addIllegalOp<mhlo::ComplexOp>();
    target.addIllegalOp<mhlo::ImagOp>();
    target.addIllegalOp<mhlo::RealOp>();

    // Binary elementwise.
    target.addDynamicallyLegalOp<mhlo::AddOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<chlo::BroadcastAddOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<mhlo::SubOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<chlo::BroadcastSubOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<mhlo::MulOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<chlo::BroadcastMulOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<mhlo::DivOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<chlo::BroadcastDivOp>(hasNoComplexTypes);

    // Unary.
    target.addDynamicallyLegalOp<mhlo::AbsOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<mhlo::ExpOp>(hasNoComplexTypes);

    // Compare.
    target.addDynamicallyLegalOp<mhlo::CompareOp>(hasNoComplexTypes);
    target.addDynamicallyLegalOp<chlo::BroadcastCompareOp>(hasNoComplexTypes);

    if (failed(applyPartialConversion(getOperation(), target,
                                      std::move(patterns)))) {
      return signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<FuncOp>>
createTestMHLOConvertComplexToRealPass() {
  return std::make_unique<TestMHLOConvertComplexToRealPass>();
}

}  // namespace MHLO
}  // namespace iree_compiler
}  // namespace mlir
