// Copyright 2022 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Util/IR/UtilDialect.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "iree/compiler/Dialect/Util/IR/UtilTraits.h"
#include "iree/compiler/Dialect/Util/Transforms/Passes.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Util {

namespace {

class StripDebugOpsPass
    : public PassWrapper<StripDebugOpsPass, OperationPass<void>> {
 public:
  StringRef getArgument() const override { return "iree-util-strip-debug-ops"; }

  StringRef getDescription() const override {
    return "Strips debug ops, like assertions.";
  }

  void runOnOperation() override {
    getOperation()->walk([](Operation *op) {
      if (isa<mlir::AssertOp>(op) ||
          op->hasTrait<OpTrait::IREE::Util::DebugOnly>()) {
        op->erase();
      }
    });
  }
};

}  // namespace

std::unique_ptr<OperationPass<void>> createStripDebugOpsPass() {
  return std::make_unique<StripDebugOpsPass>();
}

static PassRegistration<StripDebugOpsPass> pass;

}  // namespace Util
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
