// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//===-LegalizeNumWorkgroupsFnPass.cpp - Legalize to be runnable on host ---===//
//
// The function generated by the codegeneration pass to compute the number of
// workgroups uses a slice of the device-side code. Legalize it to run on the
// host.
//
//===----------------------------------------------------------------------===//
#include "iree/compiler/Conversion/CodegenUtils/FunctionUtils.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/Attributes.h"
#include "iree/compiler/Dialect/IREE/IR/IREEOps.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
namespace iree_compiler {

namespace {

/// Pattern to legalize shapex.tie_shape operation to tie the shape of the
/// `iree.placeholder` result to the argument of the function.
struct LegalizeTieShapeOp : OpRewritePattern<Shape::TieShapeOp> {
  using OpRewritePattern<Shape::TieShapeOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(Shape::TieShapeOp tieShapeOp,
                                PatternRewriter &rewriter) const override {
    if (tieShapeOp.shape().isa<BlockArgument>()) return failure();
    auto phOp = dyn_cast_or_null<IREE::PlaceholderOp>(
        tieShapeOp.operand().getDefiningOp());
    if (!phOp) return failure();
    IntegerAttr operandNumAttr =
        phOp.getAttrOfType<IntegerAttr>(getOperandResultNumAttrName());
    if (!operandNumAttr) {
      return phOp.emitRemark("expected operand_result_index attribute");
    }
    FuncOp numWorkgroupsFn = phOp.getParentOfType<FuncOp>();
    rewriter.replaceOpWithNewOp<Shape::TieShapeOp>(
        tieShapeOp, phOp,
        numWorkgroupsFn.getArgument(
            phOp.getAttrOfType<IntegerAttr>(getOperandResultNumAttrName())
                .getInt()));
    return success();
  }
};

/// Pattern to remove dead `iree.placeholder` ops. They arent removed since they
/// are tagged as having `MemoryEffect`.
struct RemoveDeadPlaceholderOp : OpRewritePattern<IREE::PlaceholderOp> {
  using OpRewritePattern<IREE::PlaceholderOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(IREE::PlaceholderOp phOp,
                                PatternRewriter &rewriter) const override {
    if (phOp.use_empty()) {
      rewriter.eraseOp(phOp);
      return success();
    }
    return failure();
  }
};

/// Pass to legalize the function that computes the number of workgroups to use
/// for launch to be runnable on the host-side.
struct LegalizeNumWorkgroupsFnPass
    : public PassWrapper<LegalizeNumWorkgroupsFnPass, OperationPass<ModuleOp>> {
  LegalizeNumWorkgroupsFnPass() = default;
  LegalizeNumWorkgroupsFnPass(const LegalizeNumWorkgroupsFnPass &pass) {}
  void runOnOperation() override;
};
}  // namespace

static void populateLegalizeNumWorkgroupsFnPattern(
    MLIRContext *context, OwningRewritePatternList &patterns) {
  patterns.insert<LegalizeTieShapeOp, RemoveDeadPlaceholderOp>(context);
}

void LegalizeNumWorkgroupsFnPass::runOnOperation() {
  ModuleOp module = getOperation();
  auto fns = module.getOps<FuncOp>();

  OwningRewritePatternList patterns;
  MLIRContext *context = &getContext();
  populateLegalizeNumWorkgroupsFnPattern(context, patterns);

  FrozenRewritePatternList frozenPatterns(std::move(patterns));
  SymbolTable symbolTable(module.getOperation());
  for (FuncOp fn : fns) {
    if (!isEntryPoint(fn)) continue;
    auto numWorkgroupsFnAttr =
        fn.getAttrOfType<SymbolRefAttr>(getNumWorkgroupsFnAttrName());
    if (!numWorkgroupsFnAttr) continue;
    StringRef numWorkgroupsFnName = numWorkgroupsFnAttr.getLeafReference();
    FuncOp numWorkgroupsFn = symbolTable.lookup<FuncOp>(numWorkgroupsFnName);
    if (!numWorkgroupsFn) {
      fn.emitError("unable to find function to compute number of workgroups ")
          << numWorkgroupsFnName;
      return signalPassFailure();
    }
    if (failed(applyPatternsAndFoldGreedily(numWorkgroupsFn, frozenPatterns)))
      return signalPassFailure();
  }
}

std::unique_ptr<OperationPass<ModuleOp>> createLegalizeNumWorkgroupsFnPass() {
  return std::make_unique<LegalizeNumWorkgroupsFnPass>();
}

static PassRegistration<LegalizeNumWorkgroupsFnPass> pass(
    "iree-codegen-legalize-num-workgroups-fn",
    "Legalize the function that computes the number of workgroups to use to be "
    "usable on the host side",
    [] { return std::make_unique<LegalizeNumWorkgroupsFnPass>(); });

}  // namespace iree_compiler
}  // namespace mlir
