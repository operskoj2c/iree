// Copyright 2020 The IREE Authors
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
#include "llvm/ADT/StringSet.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

class TranslateExecutablesPass
    : public PassWrapper<TranslateExecutablesPass,
                         OperationPass<IREE::HAL::ExecutableTargetOp>> {
 public:
  explicit TranslateExecutablesPass(TargetOptions executableOptions)
      : executableOptions_(executableOptions) {
    for (auto &targetBackend :
         matchTargetBackends(executableOptions_.targets)) {
      auto pm = std::make_unique<OpPassManager>(
          IREE::HAL::ExecutableTargetOp::getOperationName(),
          OpPassManager::Nesting::Implicit);
      targetBackend->buildTranslationPassPipeline(*pm);
      pipelines_.push_back({std::move(targetBackend), std::move(pm)});
    }
  }

  TranslateExecutablesPass(const TranslateExecutablesPass &other)
      : TranslateExecutablesPass(other.executableOptions_) {}

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<HALDialect>();

    for (auto &pipeline : pipelines_) {
      pipeline.passManager->getDependentDialects(registry);
    }
  }

  void runOnOperation() override {
    auto targetOp = getOperation();
    for (auto &pipeline : pipelines_) {
      if (!TargetBackend::matchPattern(
              pipeline.targetBackend->filter_pattern(),
              targetOp.target_backend_filter().str())) {
        continue;
      }
      if (failed(runPipeline(*pipeline.passManager, targetOp))) {
        targetOp.emitError() << "failed to run translation of source "
                                "executable to target executable for backend "
                             << targetOp.target_backend_filter();
        return signalPassFailure();
      }
    }
  }

 private:
  struct Pipeline {
    std::unique_ptr<TargetBackend> targetBackend;
    std::unique_ptr<OpPassManager> passManager;
  };

  TargetOptions executableOptions_;
  llvm::SmallVector<Pipeline, 4> pipelines_;
};

std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createTranslateExecutablesPass(TargetOptions executableOptions) {
  return std::make_unique<TranslateExecutablesPass>(executableOptions);
}

static PassRegistration<TranslateExecutablesPass> pass(
    "iree-hal-translate-executables",
    "Translates hal.executable.target via the target backend pipelines", [] {
      auto options = getTargetOptionsFromFlags();
      return std::make_unique<TranslateExecutablesPass>(options);
    });

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
