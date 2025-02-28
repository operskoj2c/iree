// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Main entry function for iree-opt and derived binaries.
//
// Based on mlir-opt but registers the passes and dialects we care about.

#include "iree/tools/init_dialects.h"
#include "iree/tools/init_passes.h"
#include "iree/tools/init_targets.h"
#include "llvm/Support/InitLLVM.h"
#include "mlir/IR/Dialect.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);

  mlir::DialectRegistry registry;
  mlir::iree_compiler::registerAllDialects(registry);
  mlir::iree_compiler::registerAllPasses();
  mlir::iree_compiler::registerHALTargetBackends();

  if (failed(MlirOptMain(argc, argv, "IREE modular optimizer driver\n",
                         registry,
                         /*preloadDialectsInContext=*/false))) {
    return 1;
  }
  return 0;
}
