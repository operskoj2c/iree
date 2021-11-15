// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Conversion2/StandardToHAL/ConvertStandardToHAL.h"

#include "iree/compiler/Dialect/HAL/IR/HALDialect.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/IR/HALTypes.h"
#include "mlir/Dialect/Shape/IR/Shape.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

void populateStandardShapeToHALPatterns(MLIRContext *context,
                                        ConversionTarget &conversionTarget,
                                        OwningRewritePatternList &patterns,
                                        TypeConverter &converter);

void populateStandardStructuralToHALPatterns(MLIRContext *context,
                                             ConversionTarget &conversionTarget,
                                             OwningRewritePatternList &patterns,
                                             TypeConverter &converter);

void populateStandardToHALPatterns(MLIRContext *context,
                                   ConversionTarget &conversionTarget,
                                   TypeConverter &typeConverter,
                                   OwningRewritePatternList &patterns) {
  populateStandardShapeToHALPatterns(context, conversionTarget, patterns,
                                     typeConverter);
  populateStandardStructuralToHALPatterns(context, conversionTarget, patterns,
                                          typeConverter);
}

}  // namespace iree_compiler
}  // namespace mlir
