// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/InputConversion/Common/PassDetail.h"
#include "iree/compiler/InputConversion/Common/Passes.h"
#include "mlir/Conversion/SCFToStandard/SCFToStandard.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

namespace {

struct TopLevelSCFToCFGPass
    : public TopLevelSCFToCFGBase<TopLevelSCFToCFGPass> {
  void runOnOperation() override;
};

}  // namespace

void TopLevelSCFToCFGPass::runOnOperation() {
  RewritePatternSet patterns(&getContext());
  populateLoopToStdConversionPatterns(patterns);
  // Configure conversion to lower out scf.for, scf.if, scf.parallel and
  // scf.while. Anything else is fine.
  ConversionTarget target(getContext());
  target.addIllegalOp<scf::ForOp, scf::IfOp, scf::ParallelOp, scf::WhileOp>();
  target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

  // For nested, opaque ops that we support, mark them recursively legal.
  // Otherwise, SCF within them will be processed by this pass.
  // It would be nice to be able to set this for the whole dialect, but
  // upstream does not support that yet.
  target.addLegalOp<linalg::GenericOp>();
  target.markOpRecursivelyLegal<linalg::GenericOp>();

  if (failed(
          applyPartialConversion(getOperation(), target, std::move(patterns))))
    signalPassFailure();
}

std::unique_ptr<OperationPass<FuncOp>> createTopLevelSCFToCFGPass() {
  return std::make_unique<TopLevelSCFToCFGPass>();
}

}  // namespace iree_compiler
}  // namespace mlir
