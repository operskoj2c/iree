// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_INPUTCONVERSION_COMMON_PASSES_H_
#define IREE_COMPILER_INPUTCONVERSION_COMMON_PASSES_H_

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

//===----------------------------------------------------------------------===//
// Pipelines
//===----------------------------------------------------------------------===//

// Performs common input legalization after specific input dialect conversions
// have taken place.
void buildCommonInputConversionPassPipeline(OpPassManager &passManager);

//===----------------------------------------------------------------------===//
// Passes
//===----------------------------------------------------------------------===//

std::unique_ptr<OperationPass<FuncOp>> createTopLevelSCFToCFGPass();
std::unique_ptr<OperationPass<ModuleOp>> createIREEImportPublicPass();

//===----------------------------------------------------------------------===//
// Register all Passes
//===----------------------------------------------------------------------===//

void registerCommonInputConversionPasses();

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_INPUTCONVERSION_COMMON_PASSES_H_
