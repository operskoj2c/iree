// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_DIALECT_HAL_CONVERSION_FLOWTOHAL_CONVERTFLOWTOHAL_H_
#define IREE_COMPILER_DIALECT_HAL_CONVERSION_FLOWTOHAL_CONVERTFLOWTOHAL_H_

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

// Adds op legality rules to |conversionTarget| to ensure all incoming flow ops
// are removed during Flow->HAL lowering.
void setupFlowToHALLegality(MLIRContext *context,
                            ConversionTarget &conversionTarget,
                            TypeConverter &typeConverter);

// Populates conversion patterns for Flow->HAL.
void populateFlowToHALPatterns(MLIRContext *context,
                               OwningRewritePatternList &patterns,
                               TypeConverter &typeConverter);

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_HAL_CONVERSION_FLOWTOHAL_CONVERTFLOWTOHAL_H_
