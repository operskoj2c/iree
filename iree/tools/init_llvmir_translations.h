// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Configures a context with hooks to translate custom extensions to LLVMIR.
// Note that this has nothing to do with the named translations that are
// globally registered as part of init_translations.h for the purpose of
// driving iree-translate. This is maintained separately to other dialect
// initializations because it causes a transitive dependency on LLVMIR.

#ifndef IREE_TOOLS_INIT_LLVMIR_TRANSLATIONS_H_
#define IREE_TOOLS_INIT_LLVMIR_TRANSLATIONS_H_

#include "mlir/Target/LLVMIR/Dialect/ArmNeon/ArmNeonToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"

namespace mlir {
namespace iree_compiler {

inline void registerLLVMIRTranslations(DialectRegistry &registry) {
  mlir::registerLLVMDialectTranslation(registry);
  mlir::registerArmNeonDialectTranslation(registry);
}

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_TOOLS_INIT_LLVMIR_TRANSLATIONS_H_
