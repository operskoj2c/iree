// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <memory>
#include <utility>

#include "iree/compiler/Dialect/HAL/IR/HALDialect.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/Target/TargetBackend.h"
#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/Dialect/HAL/Transforms/Passes.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

class VerifyTargetEnvironmentPass
    : public PassWrapper<VerifyTargetEnvironmentPass, OperationPass<ModuleOp>> {
 public:
  VerifyTargetEnvironmentPass() = default;

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<IREE::HAL::HALDialect>();
  }

  StringRef getArgument() const override {
    return "iree-hal-verify-target-environment";
  }

  StringRef getDescription() const override {
    return "Verifies that the target execution environment is valid.";
  }

  void runOnOperation() override {
    auto moduleOp = getOperation();

    // Must have targets specified.
    auto targetsAttr = moduleOp->getAttrOfType<ArrayAttr>("hal.device.targets");
    if (!targetsAttr || targetsAttr.empty()) {
      auto diagnostic = moduleOp.emitError();
      diagnostic
          << "no HAL target devices specified on the module (available = [ ";
      for (const auto &targetName : getRegisteredTargetBackends()) {
        diagnostic << "'" << targetName << "' ";
      }
      diagnostic << "])";
      signalPassFailure();
      return;
    }

    // Verify each target is registered.
    for (auto attr : targetsAttr) {
      auto targetAttr = attr.dyn_cast<IREE::HAL::DeviceTargetAttr>();
      if (!targetAttr) {
        moduleOp.emitError() << "invalid target attr type: " << attr;
        signalPassFailure();
        return;
      }

      auto targetBackend =
          IREE::HAL::getTargetBackend(targetAttr.getDeviceID().getValue());
      if (!targetBackend) {
        auto diagnostic = moduleOp.emitError();
        diagnostic
            << "unregistered target backend " << targetAttr.getDeviceID()
            << "; ensure it is linked in to the compiler (available = [ ";
        for (const auto &targetName : getRegisteredTargetBackends()) {
          diagnostic << "'" << targetName << "' ";
        }
        diagnostic << "])";
        signalPassFailure();
        return;
      }
    }
  }
};

std::unique_ptr<OperationPass<ModuleOp>> createVerifyTargetEnvironmentPass() {
  return std::make_unique<VerifyTargetEnvironmentPass>();
}

static PassRegistration<VerifyTargetEnvironmentPass> pass([] {
  return std::make_unique<VerifyTargetEnvironmentPass>();
});

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
