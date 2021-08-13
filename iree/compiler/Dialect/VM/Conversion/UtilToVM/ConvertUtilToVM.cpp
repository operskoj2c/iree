// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/VM/Conversion/UtilToVM/ConvertUtilToVM.h"

#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "iree/compiler/Dialect/Util/IR/UtilTypes.h"
#include "iree/compiler/Dialect/VM/Conversion/TypeConverter.h"
#include "iree/compiler/Dialect/VM/IR/VMOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

void populateUtilGlobalToVMPatterns(MLIRContext *context,
                                    ConversionTarget &conversionTarget,
                                    TypeConverter &typeConverter,
                                    OwningRewritePatternList &patterns);
void populateUtilListToVMPatterns(MLIRContext *context,
                                  TypeConverter &typeConverter,
                                  OwningRewritePatternList &patterns);
void populateUtilStatusToVMPatterns(MLIRContext *context,
                                    ConversionTarget &conversionTarget,
                                    TypeConverter &typeConverter,
                                    OwningRewritePatternList &patterns);

namespace {

//===----------------------------------------------------------------------===//
// util.null
//===----------------------------------------------------------------------===//

class NullOpConversion : public OpConversionPattern<IREE::Util::NullOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      IREE::Util::NullOp op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<IREE::VM::ConstRefZeroOp>(
        op, IREE::VM::RefType::get(op.getType()));
    return success();
  }
};

//===----------------------------------------------------------------------===//
// util.byte_buffer.*
//===----------------------------------------------------------------------===//

class ByteBufferConstantOpConversion
    : public OpConversionPattern<IREE::Util::ByteBufferConstantOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      IREE::Util::ByteBufferConstantOp op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<IREE::VM::RodataInlineOp>(
        op,
        IREE::VM::RefType::get(
            IREE::VM::BufferType::get(rewriter.getContext())),
        op.value());
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Compiler hints
//===----------------------------------------------------------------------===//

class UnreachableOpConversion
    : public OpConversionPattern<IREE::Util::UnreachableOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      IREE::Util::UnreachableOp srcOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<IREE::VM::FailOp>(
        srcOp,
        rewriter.createOrFold<IREE::VM::ConstI32Op>(
            srcOp.getLoc(),
            static_cast<int32_t>(IREE::Util::StatusCode::Unknown)),
        srcOp.message());
    return success();
  }
};

}  // namespace

void populateUtilToVMPatterns(MLIRContext *context,
                              ConversionTarget &conversionTarget,
                              TypeConverter &typeConverter,
                              OwningRewritePatternList &patterns) {
  patterns.insert<NullOpConversion>(typeConverter, context);
  patterns.insert<ByteBufferConstantOpConversion>(typeConverter, context);
  patterns.insert<UnreachableOpConversion>(typeConverter, context);

  typeConverter.addConversion(
      [](IREE::Util::ByteBufferType type) -> Optional<Type> {
        return IREE::VM::RefType::get(
            IREE::VM::BufferType::get(type.getContext()));
      });
  typeConverter.addConversion(
      [](IREE::Util::MutableByteBufferType type) -> Optional<Type> {
        return IREE::VM::RefType::get(
            IREE::VM::BufferType::get(type.getContext()));
      });

  populateUtilGlobalToVMPatterns(context, conversionTarget, typeConverter,
                                 patterns);
  populateUtilListToVMPatterns(context, typeConverter, patterns);
  populateUtilStatusToVMPatterns(context, conversionTarget, typeConverter,
                                 patterns);
}

}  // namespace iree_compiler
}  // namespace mlir
