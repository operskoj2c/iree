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

#include "iree/compiler/Dialect/Flow/IR/FlowDialect.h"
#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/Flow/IR/FlowTypes.h"
#include "iree/compiler/Dialect/Flow/Transforms/DestructiveUpdateUtils.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeDialect.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "llvm/ADT/STLExtras.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/TypeRange.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/Transforms/RegionUtils.h"

#define DEBUG_TYPE "iree-flow-dispatch-linalg-on-tensors"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

static unsigned kNumMaxParallelDims = 3;

/// PatternRewriter that allows replacing only a subset of uses.
/// Since this only adds a method, it can just be static_cast'ed to when
/// applying a rewrite.
/// TODO(nicolasvasilache): upstream support for this is landing, rebase on that
struct PatternRewriterWithScopedReplaceOp : public PatternRewriter {
  void replaceOpWithinScope(Operation *op, ValueRange newValues, Block *block) {
    // Notify the rewriter subclass that we're about to replace this root.
    notifyRootReplaced(op);

    assert(op->getNumResults() == newValues.size() &&
           "incorrect # of replacement values");
    bool erase = true;
    SmallVector<Operation *, 4> ops;
    SmallVector<Value, 4> operands, repls;
    for (auto &use : op->getUses()) {
      if (!block->getParentOp()->isProperAncestor(use.getOwner())) {
        erase = false;
        continue;
      }
      OpResult opResult = use.get().cast<OpResult>();
      ops.push_back(use.getOwner());
      operands.push_back(use.get());
      repls.push_back(newValues[opResult.getResultNumber()]);
    }
    // Perform the actual replacements.
    for (auto it : llvm::zip(ops, operands, repls))
      std::get<0>(it)->replaceUsesOfWith(std::get<1>(it), std::get<2>(it));
    if (erase) {
      notifyOperationRemoved(op);
      op->erase();
    }
  }
};

struct DispatchLinalgOnTensorsPass
    : public PassWrapper<DispatchLinalgOnTensorsPass, OperationPass<FuncOp>> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<linalg::LinalgDialect, IREE::Flow::FlowDialect,
                    AffineDialect, scf::SCFDialect, ShapeDialect>();
  }
  DispatchLinalgOnTensorsPass() = default;
  DispatchLinalgOnTensorsPass(ArrayRef<int64_t> sizes,
                              bool enableFusion = false) {
    this->tileSizes = sizes;
    this->enableFusion = enableFusion;
  };
  DispatchLinalgOnTensorsPass(const DispatchLinalgOnTensorsPass &pass) {}
  void runOnOperation() override;

 private:
  ListOption<int64_t> tileSizes{
      *this, "tile-sizes", llvm::cl::desc("Set tile sizes to use"),
      llvm::cl::ZeroOrMore, llvm::cl::MiscFlags::CommaSeparated};
  Option<bool> enableFusion{
      *this, "enable-fusion",
      llvm::cl::desc("Enable fusion on linalg on tensors path"),
      llvm::cl::init(false)};
};

/// Returns the number of consecutive outer loops that are "parallel". This is a
/// copy of the function from
/// iree/compiler/Conversion/CodegenUtils/FunctionUtils.h that is duplicated
/// here to avoid adding an build dependency.
static size_t getNumOuterParallelLoops(linalg::LinalgOp op) {
  return op.iterator_types()
      .getValue()
      .take_while([](Attribute attr) -> bool {
        return linalg::isParallelIteratorType(attr);
      })
      .size();
}

/// Returns the number of loops of the operation that are to be tiled.
static size_t getNumTilableLoops(linalg::LinalgOp op) {
  return std::min<size_t>(getNumOuterParallelLoops(op), kNumMaxParallelDims);
}

// Creates a flow.dispatch.workgroup op without arguments.
// All the necessary operands are transiently captured and rewritten late as
// operands. This greatly simplifies transformations into the resulting op.
static IREE::Flow::DispatchWorkgroupsOp buildOperandLessFlowDispatchWorkgroupOp(
    PatternRewriter &rewriter, linalg::LinalgOp root,
    linalg::LinalgOp &clonedRoot) {
  Location loc = root->getLoc();
  Value one = rewriter.create<ConstantIndexOp>(loc, 1);
  SmallVector<Value, 4> count = llvm::to_vector<4>(llvm::map_range(
      root.createLoopRanges(rewriter, loc), [](Range r) { return r.size; }));
  count.resize(getNumTilableLoops(root));
  count = llvm::to_vector<4>(llvm::reverse(count));
  count.resize(kNumMaxParallelDims, one);

  auto dispatchOp = rewriter.create<IREE::Flow::DispatchWorkgroupsOp>(
      loc, count, root->getResultTypes(), ValueRange{});
  Region &region = dispatchOp.body();
  Block *block = &region.front();
  {
    OpBuilder::InsertionGuard g(rewriter);
    rewriter.setInsertionPointToStart(block);
    clonedRoot = cast<linalg::LinalgOp>(rewriter.clone(*root.getOperation()));
    // Note: DispatchOutputStoreOp is an abstraction jump that consumes the SSA
    // value produced by `clonedRoot` but it does not comply with the semantics
    // of DispatchWorkgroupsOp which explicitly states:
    // "behavior is undefined if multiple workgroups store to the same regions
    // of the output tensors".
    // Similarly to sequentialized SPMD loops, the semantics is valid assuming a
    // sequential ordering of execution.
    // After destructive update rewrites, the abstraction gap disappears.
    for (auto it : llvm::zip(clonedRoot->getResults(),
                             dispatchOp.body().getArguments().take_back(
                                 clonedRoot->getNumResults()))) {
      rewriter.create<IREE::Flow::DispatchOutputStoreOp>(
          loc, std::get<0>(it), std::get<1>(it), llvm::None, llvm::None,
          llvm::None);
    }
    // TODO(nicolasvasilache): return `clonedRoot->getResults()` once we have
    // shape operands and we drop tie_shape.
    rewriter.create<IREE::Flow::ReturnOp>(loc);
  }
  LLVM_DEBUG(llvm::dbgs() << "Created dispatchOp shell " << *dispatchOp
                          << "\n");
  return dispatchOp;
}

// Only fuses the first producer for the purpose of connecting the pieces.
// The impl does not worry about the dispatchOp, operands and arguments are set
// in a post-pattern `legalizeDispatchWorkgroupOperands` function.
// To simplify the implementation of the dispatch region formation, we just
// clone the op that needs to be fused inside the dispatch region and just fuse
// that one. This avoid any concerns related to tensor operands that are only
// used for their DimOp. This is a canonicalization that is more involved than
// necessary across the boundary of regions without captures.
//
// TODO(nicolasvasilache): Enhance fusion.
//
// TODO(nicolasvasilache: This implementation jumps an abstraction gap as it
// knows that `clonedLinalgOp` has been tiled into `tiledLinalgOp`. In the case
// of `out` tensors, this allows calling into a `fuseProducerOfTensor` to which
// we provide the producer by construction. This avoids an analysis that would
// need to reconstruct a destructive update from the loop nest + operations in
// order to get the producer of an `out` tensor.
// In the future, this analysis should be implemented in core but for now it is
// IREE-only.
static Optional<linalg::FusionInfo> pullInFirstProducerOf(
    PatternRewriter &rewriter, IREE::Flow::DispatchWorkgroupsOp dispatchOp,
    ValueRange shapedOperands, linalg::TiledLinalgOp &tiledLinalgOp) {
  // Scoped within DispatchWorkgroupOp.
  OpBuilder::InsertionGuard g(rewriter);
  rewriter.setInsertionPointToStart(&dispatchOp.getRegion().front());
  for (auto en : llvm::enumerate(shapedOperands)) {
    if (auto linalgOp = en.value().getDefiningOp<linalg::LinalgOp>()) {
      Operation *clonedOpToFuse = rewriter.clone(*linalgOp);
      static_cast<PatternRewriterWithScopedReplaceOp &>(rewriter)
          .replaceOpWithinScope(linalgOp, clonedOpToFuse->getResults(),
                                &dispatchOp.getRegion().front());
      // TODO: this is incorrect on general pattern failures, try pattern within
      // pattern.
      OpResult opResult = en.value().cast<OpResult>();
      auto maybeFusionInfo = linalg::fuseProducerOfTensor(
          rewriter, clonedOpToFuse->getResult(opResult.getResultNumber()),
          tiledLinalgOp.op.getShapedOpOperand(en.index()));
      if (!maybeFusionInfo.hasValue()) {
        rewriter.replaceOp(clonedOpToFuse, linalgOp->getResults());
      }
      return maybeFusionInfo;
    }
  }
  return llvm::None;
}

// Add tie_shape for all outputs. This provides necessary information for
// a subsequent OutlineDispatchRegion2 pass invocation to work properly.
// TODO(nicolasvasilache): get rid of this once we have a proper shape +
// subshape in core and DispatchWorkgroupOp takes output shape parameters.
static SmallVector<Value, 4> createDispatchTieShapeOp(
    PatternRewriter &rewriter, linalg::LinalgOp linalgOp,
    IREE::Flow::DispatchWorkgroupsOp dispatchOp) {
  assert(dispatchOp->getNumResults() == linalgOp.getNumOutputs());
  MLIRContext *context = linalgOp->getContext();
  Location loc = linalgOp->getLoc();
  SmallVector<Value, 4> shapedResults;
  for (auto it : llvm::zip(linalgOp.getOutputs(), dispatchOp->getResults())) {
    // Insert DimOp and MakeRankedShapeOp just before the dispatchOp to play
    // nicely with a (much later) OutlineDispatchRegions2 which requires all
    // dims and shapes to dominate the dispatchOp region.
    OpBuilder::InsertionGuard g(rewriter);
    rewriter.setInsertionPoint(dispatchOp);

    assert(std::get<0>(it).getType() == std::get<1>(it).getType());
    auto rankedTensorType = std::get<0>(it).getType().cast<RankedTensorType>();
    if (rankedTensorType.hasStaticShape()) {
      shapedResults.push_back(std::get<1>(it));
      continue;
    }
    auto rank = rankedTensorType.getRank();
    SmallVector<Value, 4> dims;
    dims.reserve(rank);
    for (unsigned d = 0, e = rank; d < e; ++d) {
      if (rankedTensorType.isDynamicDim(d)) {
        dims.push_back(rewriter.create<DimOp>(loc, std::get<0>(it), d));
      }
    }
    auto shapeOp = rewriter.create<Shape::MakeRankedShapeOp>(
        loc, Shape::RankedShapeType::get(rankedTensorType.getShape(), context),
        dims);

    // The TieShapeOp use the dispatchOp results.
    rewriter.setInsertionPointAfter(dispatchOp);
    shapedResults.push_back(
        rewriter.create<Shape::TieShapeOp>(loc, std::get<1>(it), shapeOp));
  }
  return shapedResults;
}

// Rewrite pattern to ensure only ops with tensor semantics are tiled.
struct TileAndDistributeOnTensorsPattern
    : public linalg::LinalgBaseTilingPattern {
  using Base = linalg::LinalgBaseTilingPattern;
  TileAndDistributeOnTensorsPattern(linalg::LinalgTilingOptions options,
                                    linalg::LinalgTransformationFilter marker,
                                    bool enableFusion,
                                    PatternBenefit benefit = 1)
      : Base(options, marker, benefit), enableFusion(enableFusion) {}

  LogicalResult matchAndRewrite(Operation *op,
                                PatternRewriter &rewriter) const override {
    auto linalgOp = dyn_cast<linalg::LinalgOp>(op);
    if (!linalgOp || !linalgOp.hasTensorSemantics()) return failure();

    // TODO(ravishankarm): Until fusion is handled properly, only tile and
    // distribute for matmul operations.
    if (enableFusion && !isa<linalg::MatmulOp>(op)) return failure();

    linalg::LinalgOp clonedLinalgOp;
    IREE::Flow::DispatchWorkgroupsOp dispatchOp =
        buildOperandLessFlowDispatchWorkgroupOp(rewriter, linalgOp,
                                                clonedLinalgOp);
    // Scoped within DispatchWorkgroupOp.
    OpBuilder::InsertionGuard g(rewriter);
    rewriter.setInsertionPoint(clonedLinalgOp);

    linalg::TiledLinalgOp tiledLinalgOp;
    LogicalResult tilingResult =
        Base::matchAndRewriteBase(clonedLinalgOp, rewriter, tiledLinalgOp);
    if (failed(tilingResult)) {
      // GreedyPatternRewriter is not transactional and does not stop on
      // failure. Must explicitly delete on all failure paths.
      rewriter.eraseOp(dispatchOp);
      return failure();
    }
    // Keep track of the shapedOperands for fusion.
    SmallVector<Value, 4> shapedOperands(clonedLinalgOp.getShapedOperands());
    rewriter.replaceOp(clonedLinalgOp, tiledLinalgOp.tensorResults);

    pullInFirstProducerOf(rewriter, dispatchOp, shapedOperands, tiledLinalgOp);

    SmallVector<Value, 4> shapedResults =
        createDispatchTieShapeOp(rewriter, linalgOp, dispatchOp);
    rewriter.replaceOp(op, shapedResults);
    return success();
  }

  const bool enableFusion;
};

template <typename OpTy>
static Value buildFlowWorkgroupInfoOp(OpBuilder &b, unsigned dim) {
  return b.template create<OpTy>(b.getInsertionPoint()->getLoc(), dim);
}

// After outlining in dispatch region we can rewrite the dispatch ops with
// proper captures.
// A later RematerializeDispatchConstants should be called to avoid passing
// unnecessary constant arguments.
static void legalizeDispatchWorkgroupOperands(
    IREE::Flow::DispatchWorkgroupsOp dispatchOp) {
  Location loc = dispatchOp.getLoc();
  Block &block = dispatchOp.body().front();
  unsigned numOldBBArgs = block.getNumArguments();
  OpBuilder b = OpBuilder::atBlockBegin(&block);

  llvm::SetVector<Value> valuesSet;
  mlir::getUsedValuesDefinedAbove(dispatchOp.body(), valuesSet);

  auto getUsesOfValueOutsideOfDispatchOp =
      [&](Value v) -> SmallVector<Operation *, 4> {
    SmallVector<Operation *, 4> res;
    for (Operation *user : v.getUsers())
      if (!dispatchOp->isAncestor(user)) res.push_back(user);
    return res;
  };

  // Go through the captured values and check for any `init_tensor`. These can
  // be pulled into the dispatch region
  BlockAndValueMapping map;
  for (Value operand : valuesSet) {
    auto initTensorOp = operand.getDefiningOp<linalg::InitTensorOp>();
    if (!initTensorOp) continue;
    auto clonedOp =
        cast<linalg::InitTensorOp>(b.clone(*initTensorOp.getOperation(), map));
    auto outsideUses = getUsesOfValueOutsideOfDispatchOp(operand);
    operand.replaceAllUsesExcept(
        clonedOp.getResult(),
        SmallPtrSet<Operation *, 8>(outsideUses.begin(), outsideUses.end()));
  }

  // Recompute the values captured from outside.
  valuesSet.clear();
  mlir::getUsedValuesDefinedAbove(dispatchOp.body(), valuesSet);
  ValueRange valuesDefinedAbove{valuesSet.getArrayRef()};

  // Replace valuesDefinedAbove by new BB args (including the op's operands).
  for (Value operand : valuesDefinedAbove) {
    if (auto rt = operand.getType().dyn_cast<RankedTensorType>()) {
      block.addArgument(IREE::Flow::DispatchInputType::get(
          rt.getShape(), rt.getElementType()));
    } else {
      block.addArgument(operand.getType());
    }

    Value bbArg = block.getArguments().back();
    auto uses = getUsesOfValueOutsideOfDispatchOp(operand);
    Value repl = bbArg;
    if (bbArg.getType().isa<IREE::Flow::DispatchInputType>()) {
      repl = b.create<IREE::Flow::DispatchInputLoadOp>(loc, operand.getType(),
                                                       bbArg);
    } else if (bbArg.getType().isa<IREE::Flow::DispatchOutputType>()) {
      // TODO(nicolasvasilache): do something useful.
      continue;
    }
    operand.replaceAllUsesExcept(
        repl, SmallPtrSet<Operation *, 8>(uses.begin(), uses.end()));
  }

  // Reinsert and replace old BB args.
  for (BlockArgument ba : block.getArguments().take_front(numOldBBArgs)) {
    block.addArgument(ba.getType());
    ba.replaceAllUsesWith(block.getArguments().back());
  }

  // Drop old BB args.
  block.eraseArguments(
      llvm::to_vector<4>(llvm::seq<unsigned>(0, numOldBBArgs)));

  // Set the operands.
  dispatchOp.operandsMutable().assign(valuesDefinedAbove);
}

void DispatchLinalgOnTensorsPass::runOnOperation() {
  FuncOp funcOp = getOperation();
  // `isEntryPoint` functions are the ones that are marked public.
  if (!funcOp.isPublic()) return;

  MLIRContext *context = funcOp->getContext();
  context->allowUnregisteredDialects(true);

  // Distribution strategy along at most 3 dimensions with WorkgroupIdOp in
  // range [0, WorkgroupSizeOp).
  static linalg::LinalgLoopDistributionOptions workgroupDistributionOptions = {
      [](OpBuilder &builder, Location loc, ArrayRef<Range> parallelLoopRanges) {
        auto numParallelDims = parallelLoopRanges.size();
        SmallVector<linalg::ProcInfo, 3> procInfo(numParallelDims);
        for (size_t dim = 0;
             dim < std::min<size_t>(numParallelDims, kNumMaxParallelDims);
             ++dim) {
          procInfo[numParallelDims - dim - 1] = {
              buildFlowWorkgroupInfoOp<Flow::DispatchWorkgroupIDOp>(builder,
                                                                    dim),
              buildFlowWorkgroupInfoOp<Flow::DispatchWorkgroupCountOp>(builder,
                                                                       dim)};
        }
        return procInfo;
      },
      {linalg::DistributionMethod::Cyclic, linalg::DistributionMethod::Cyclic,
       linalg::DistributionMethod::Cyclic}};

  // Use the workgroup size as a proxy for tile size here. At the flow level
  // this represents the "workload" per processors and is not necessarily tied
  // to the workgroup size specified by the backend.
  OwningRewritePatternList patterns;
  auto linalgTilingOptions =
      linalg::LinalgTilingOptions()
          .setDistributionOptions(workgroupDistributionOptions)
          .setLoopType(linalg::LinalgTilingLoopType::Loops)
          .setTileSizeComputationFunction(
              [&](OpBuilder &builder, Operation *op) -> SmallVector<Value, 4> {
                auto numTiledLoops =
                    getNumTilableLoops(cast<linalg::LinalgOp>(op));
                SmallVector<Value, 4> useTileSizes(numTiledLoops);
                if (!tileSizes.empty()) {
                  useTileSizes.resize(
                      std::min<size_t>(tileSizes.size(), numTiledLoops));
                  return llvm::to_vector<4>(llvm::map_range(
                      ArrayRef<int64_t>(tileSizes).take_front(
                          std::min<size_t>(tileSizes.size(), numTiledLoops)),
                      [&](int64_t t) -> Value {
                        return builder.create<ConstantIndexOp>(op->getLoc(), t);
                      }));
                }
                for (size_t dim = 0; dim < numTiledLoops; ++dim) {
                  useTileSizes[numTiledLoops - dim - 1] =
                      buildFlowWorkgroupInfoOp<Flow::DispatchWorkgroupSizeOp>(
                          builder, dim);
                }
                return useTileSizes;
              });
  assert(linalgTilingOptions.distribution.hasValue());

  patterns.insert<TileAndDistributeOnTensorsPattern>(
      linalgTilingOptions,
      // TODO(nicolavasilache): use refactored `getWorkgroupMarker()`
      linalg::LinalgTransformationFilter(ArrayRef<Identifier>(),
                                         Identifier::get("workgroup", context)),
      enableFusion);

  // Add canonicalization patterns.
  linalg::populateLinalgTilingCanonicalizationPatterns(patterns, context);
  patterns.insert<linalg::AffineMinSCFCanonicalizationPattern>(context);
  (void)applyPatternsAndFoldGreedily(funcOp, std::move(patterns));

  // After outlining in dispatch region we can rewrite the dispatch ops with
  // proper captures.
  funcOp.walk(legalizeDispatchWorkgroupOperands);

  // Rewrite destructive updates and ensure no remaining store remains to the
  // full output.
  bool fail =
      funcOp
          .walk([&](IREE::Flow::DispatchWorkgroupsOp op) {
            if (failed(rewriteLinalgDestructiveUpdates(op))) {
              funcOp.emitError("Failed to rewrite destructive updates in:\n")
                  << *op.getOperation();
              return WalkResult::interrupt();
            }
            return WalkResult::advance();
          })
          .wasInterrupted();
  fail |=
      funcOp
          .walk([&](IREE::Flow::DispatchOutputStoreOp op) {
            if (op.offsets().empty() || op.sizes().empty() ||
                op.strides().empty()) {
              funcOp.emitError("Full-tensor DispatchOutputStoreOp remaining:\n")
                  << *op.getOperation();
              return WalkResult::interrupt();
            }
            return WalkResult::advance();
          })
          .wasInterrupted();
  if (fail) {
    signalPassFailure();
  }
}

std::unique_ptr<OperationPass<FuncOp>> createDispatchLinalgOnTensorsPass(
    ArrayRef<int64_t> sizes, bool enableFusion) {
  return std::make_unique<DispatchLinalgOnTensorsPass>(sizes, enableFusion);
}

static PassRegistration<DispatchLinalgOnTensorsPass> pass(
    "iree-flow-dispatch-linalg-on-tensors-pass",
    "Dispatch Linalg operations on tensors by using tile and distribute",
    [] { return std::make_unique<DispatchLinalgOnTensorsPass>(); });

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
