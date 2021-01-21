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

//===- Transforms.cpp - Transformations common to all backends ------------===//
//
// Implements transformations that are common to all backends.
//
//===----------------------------------------------------------------------===//

#include "iree/compiler/Conversion/Common/Transforms.h"

#include "iree/compiler/Conversion/CodegenUtils/FunctionUtils.h"
#include "iree/compiler/Conversion/CodegenUtils/GetNumWorkgroups.h"
#include "iree/compiler/Conversion/CodegenUtils/MarkerUtils.h"
#include "iree/compiler/Conversion/CodegenUtils/MatmulCodegenStrategy.h"
#include "iree/compiler/Conversion/Common/Attributes.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/Linalg/Analysis/DependenceAnalysis.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/Utils/Utils.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define DEBUG_TYPE "iree-linalg-tile-and-fuse"

namespace mlir {
namespace iree_compiler {

/// Apply canonicalizations related to tiling to make promotion/vectorization
/// easier.
void applyCanonicalizationPatternsForTiling(MLIRContext *context,
                                            Operation *op) {
  OwningRewritePatternList canonicalizationPatterns;
  canonicalizationPatterns.insert<AffineMinCanonicalizationPattern>(context);
  AffineApplyOp::getCanonicalizationPatterns(canonicalizationPatterns, context);
  AffineMinOp::getCanonicalizationPatterns(canonicalizationPatterns, context);
  SubViewOp::getCanonicalizationPatterns(canonicalizationPatterns, context);
  applyPatternsAndFoldGreedily(op, std::move(canonicalizationPatterns));
}

//===----------------------------------------------------------------------===//
// Helper functions for tile and fuse.
//===----------------------------------------------------------------------===//

/// Promotes views used to chain Linalg ops in `fusedOps`
/// into buffers using the allocation callback in `options`.
///
/// Once fused the fused views that are due to a RAW dependence can be promoted
/// to workgroup memory. This will make the intermediate storage dead.
static LogicalResult promoteFusedViews(OpBuilder &builder,
                                       ArrayRef<linalg::LinalgOp> fusedOps,
                                       const TileAndFuseOptions &options) {
  linalg::Aliases aliases;
  linalg::LinalgDependenceGraph dependenceGraph(aliases, fusedOps);
  auto fusableDependences =
      linalg::findAllFusableDependences(fusedOps, dependenceGraph);

  DenseSet<Value> promotedViews;
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(*fusedOps.begin());

  // Scan the list of ops in reverse order. The fusion is performed by creating
  // a tiled version of the ops within the tiled loops of the last operation in
  // the sequence, and then proceeding up the sequence.
  for (linalg::LinalgOp op : llvm::reverse(fusedOps)) {
    auto dependences = fusableDependences.lookup(op);
    if (dependences.empty()) continue;
    if (!llvm::hasSingleElement(dependences)) {
      return op.emitError(
          "unable to promote ops with multiple fusable dependences");
    }
    auto dependence = dependences.front();
    unsigned producerIdx = dependence.dependentOpView->getOperandNumber();
    linalg::LinalgOp consumer =
        cast<linalg::LinalgOp>(dependence.indexingOpView->getOwner());
    unsigned consumerIdx = dependence.indexingOpView->getOperandNumber();
    Value consumerView = consumer.getShapedOperand(consumerIdx);
    Value promotedView = nullptr;

    // If the view is already promoted, reuse that. The assumption is that the
    // view matches already.
    if (promotedViews.count(consumerView)) {
      promotedView = consumerView;
    } else if (dependence.dependenceType ==
               linalg::LinalgDependenceGraph::RAW) {
      SubViewOp promotedViewProducer =
          op.getShapedOperand(producerIdx).getDefiningOp<SubViewOp>();
      assert(promotedViewProducer &&
             "expected producer to be a subview op as well");
      Optional<linalg::PromotionInfo> promotionInfo =
          linalg::promoteSubviewAsNewBuffer(
              builder, op.getLoc(), promotedViewProducer, options.allocationFn);
      if (!promotionInfo) {
        return op.emitError("unable to promote RAW dependence");
      }
      promotedView = promotionInfo->partialLocalView;
      consumer.getOperation()->setOperand(consumerIdx, promotedView);
      promotedViews.insert(promotedView);
    }
    if (!promotedView) continue;
    op.getOperation()->setOperand(producerIdx, promotedView);
  }
  return success();
}

/// Tile+Fuse only tiles the loops that can be fused. Tile any of the unfused
/// loops in the operation based on the configuration.
static linalg::LinalgOp tileUnfusedLoops(
    OpBuilder &builder, linalg::LinalgOp linalgOp,
    const std::set<unsigned> &fusedLoopDims, ArrayRef<int64_t> tileSizesRef) {
  SmallVector<int64_t, 4> tileSizes = llvm::to_vector<4>(tileSizesRef);
  tileSizes.resize(linalgOp.getNumLoops(), 0);
  // Linalg uses tile size = 0 for a loop to indicate not tiling that loop. Set
  // the fused loops to be untiled (since they are already tiled during fusion).
  for (unsigned loopNum : fusedLoopDims) {
    tileSizes[loopNum] = 0;
  }
  if (llvm::all_of(tileSizes, [](int64_t v) { return !v; })) return linalgOp;
  Optional<linalg::TiledLinalgOp> tiledOp = tileLinalgOp(
      builder, linalgOp,
      linalg::LinalgTilingOptions().setTileSizes(tileSizes).setLoopType(
          linalg::LinalgTilingLoopType::ParallelLoops));
  if (!tiledOp) return nullptr;
  linalgOp.erase();
  return tiledOp->op;
}

/// Tiles the last operation in `fusableOps` and fuses all other operations with
/// it by creating tiled versions of each within the generated inter-tile loops.
static Optional<linalg::TiledAndFusedLinalgOps> tileAndFuseLinalgOps(
    OpBuilder &builder, FuncOp funcOp, ArrayRef<linalg::LinalgOp> fusableOps,
    const linalg::LinalgDependenceGraph &dependenceGraph,
    ArrayRef<int64_t> tileSizes, const TileAndFuseOptions &options) {
  // Get the tile sizes to use from the last fusable op and the tile+fuse all
  // ops.
  linalg::LinalgTilingOptions tilingOptions;
  tilingOptions.setDistributionOptions(options.distributionOptions)
      .setTileSizes(tileSizes)
      .setLoopType(linalg::LinalgTilingLoopType::ParallelLoops);

  Optional<linalg::TiledAndFusedLinalgOps> tiledAndFusedOps = llvm::None;
  if (fusableOps.size() == 1) {
    linalg::LinalgOp linalgOp = fusableOps.front();
    Optional<linalg::TiledLinalgOp> tiledOp =
        tileLinalgOp(builder, linalgOp, tilingOptions);
    if (!tiledOp) {
      linalgOp.emitError("unable to tile operation");
      return llvm::None;
    }
    tiledAndFusedOps = linalg::TiledAndFusedLinalgOps{tiledOp->op, {}, {}, {}};
    auto seq = llvm::seq<unsigned>(0, tileSizes.size());
    tiledAndFusedOps->fusedLoopDims.insert(seq.begin(), seq.end());
    tiledAndFusedOps->fusedLoops.assign(tiledOp->loops.begin(),
                                        tiledOp->loops.end());
  } else {
    tiledAndFusedOps = tileAndFuseLinalgOps(builder, fusableOps,
                                            dependenceGraph, tilingOptions);
  }
  if (!tiledAndFusedOps) {
    funcOp.emitError("tile and fuse of linalg operations failed");
    return llvm::None;
  }

  // Update the launch configuration.
  SmallVector<unsigned, 2> distributedLoops =
      llvm::to_vector<2>(tiledAndFusedOps->fusedLoopDims);
  if (funcOp->getAttr(getNumWorkgroupsFnAttrName()) &&
      failed(createNumWorkgroupsFromResultShape(
          builder, fusableOps.back(), funcOp, getNumWorkgroupsFnAttrName(),
          tileSizes, distributedLoops))) {
    funcOp.emitError("failed to update launch configuration");
    return llvm::None;
  }

  // Delete all the original operations.
  for (auto linalgOp : fusableOps) linalgOp.erase();

  // Add workgroup markers to all the tiled and fused operations.
  for (auto fusedProducer : tiledAndFusedOps->fusedProducers) {
    setMarker(fusedProducer, getWorkgroupMarker());
  }
  setMarker(tiledAndFusedOps->op, getWorkgroupMarker());

  return tiledAndFusedOps;
}

LogicalResult getLinalgOps(FuncOp funcOp,
                           SmallVectorImpl<linalg::LinalgOp> &linalgOps,
                           SmallVectorImpl<Operation *> &tiledLoops) {
  Region &region = funcOp.body();
  if (!llvm::hasSingleElement(region)) {
    return funcOp.emitError("unable dispatch function with multiple blocks");
  }
  Block *body = &region.front();
  auto forOps = body->getOps<scf::ForOp>();
  while (!forOps.empty()) {
    if (!llvm::hasSingleElement(forOps)) return failure();
    scf::ForOp forOp = *(forOps.begin());
    tiledLoops.push_back(forOp.getOperation());
    body = forOp.getBody();
    forOps = body->getOps<scf::ForOp>();
  }
  linalgOps = llvm::to_vector<4>(body->getOps<linalg::LinalgOp>());
  return success();
}

namespace {
static size_t kMaxHALDimensions = 3;

/// Sets the flow.dispatch.workgroup_size operation to the constant value passed
/// in as `tileSizes`. The number of entries in `tileSizes` is at least as much
/// as the dimensionality of the workgroup. It is assumed that the inner-most
/// loop is mapped to the fastest varying dimension in
/// flow.dispatch.workgroup_size.
class SetWorkgroupSizePattern
    : public OpRewritePattern<IREE::HAL::InterfaceWorkgroupSizeOp> {
 public:
  SetWorkgroupSizePattern(MLIRContext *context, ArrayRef<int64_t> tileSizesRef,
                          PatternBenefit benefit = 1)
      : OpRewritePattern(context, benefit),
        tileSizes(
            llvm::to_vector<4>(tileSizesRef.size() > kMaxHALDimensions
                                   ? tileSizesRef.take_front(kMaxHALDimensions)
                                   : tileSizesRef)) {}

  LogicalResult matchAndRewrite(
      IREE::HAL::InterfaceWorkgroupSizeOp workgroupSizeOp,
      PatternRewriter &rewriter) const override {
    int64_t dim = workgroupSizeOp.dimension().getSExtValue();
    if (dim >= tileSizes.size()) {
      return workgroupSizeOp.emitRemark(
          "expected at least as many static tile sizes as the workgroup "
          "dimensionality");
    }
    rewriter.replaceOpWithNewOp<ConstantIndexOp>(
        workgroupSizeOp, tileSizes[tileSizes.size() - 1 - dim]);
    return success();
  }

 private:
  SmallVector<int64_t, 4> tileSizes;
};
}  // namespace

LogicalResult materializeStaticLaunchInformation(
    FuncOp funcOp, const LaunchConfig &launchConfig, unsigned numTiledLoops) {
  OwningRewritePatternList patterns;
  Optional<SmallVector<int64_t, 4>> workgroupTileSizes =
      launchConfig.getWorkgroupTileSizes(numTiledLoops);
  if (!workgroupTileSizes) {
    return funcOp.emitError(
        "unable to find static values to use for flow.dispatch.workgroup_size");
  }
  patterns.insert<SetWorkgroupSizePattern>(funcOp.getContext(),
                                           workgroupTileSizes.getValue());
  if (failed(applyPatternsAndFoldGreedily(funcOp, std::move(patterns)))) {
    return failure();
  }
  return success();
}

LogicalResult tileAndFuseLinalgBufferOps(
    FuncOp funcOp, ArrayRef<linalg::LinalgOp> linalgOps,
    const linalg::LinalgDependenceGraph &dependenceGraph,
    const LaunchConfig &launchConfig, const TileAndFuseOptions &options) {
  // Collect all operations that are to be tiled-and-fused.
  MLIRContext *context = funcOp.getContext();
  SmallVector<linalg::LinalgOp, 4> fusableOps;
  for (Operation *operation : linalgOps) {
    if (!launchConfig.hasTileSizes(operation)) continue;
    fusableOps.push_back(cast<linalg::LinalgOp>(operation));
  }
  if (fusableOps.empty()) return success();

  OpBuilder builder(context);
  ArrayRef<int64_t> tileSizes = launchConfig.getTileSizes(fusableOps.back(), 0);
  Optional<linalg::TiledAndFusedLinalgOps> tiledAndFusedOps =
      tileAndFuseLinalgOps(builder, funcOp, fusableOps, dependenceGraph,
                           tileSizes, options);
  if (!tiledAndFusedOps) {
    return funcOp.emitError("failed to tile and fuse operations");
  }

  LLVM_DEBUG({
    llvm::dbgs() << "--- After Fusion on buffers ---\n";
    funcOp.print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  applyCanonicalizationPatternsForTiling(context, funcOp);

  LLVM_DEBUG({
    llvm::dbgs() << "--- After Canonicalization ---\n";
    funcOp.print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  if (options.allocationFn) {
    SmallVector<linalg::LinalgOp, 4> promoteFusedViewOps =
        llvm::to_vector<4>(tiledAndFusedOps->fusedProducers);
    promoteFusedViewOps.push_back(tiledAndFusedOps->op);

    if (failed(promoteFusedViews(builder, promoteFusedViewOps, options))) {
      return failure();
    }
  }

  LLVM_DEBUG({
    llvm::dbgs() << "--- After Promotion ---\n";
    funcOp.print(llvm::dbgs(), OpPrintingFlags().useLocalScope());
    llvm::dbgs() << "\n\n";
  });

  // Tile the unfused loops. Set the tile sizes for the fused loops to be zero
  // to avoid tiling them again.
  for (linalg::LinalgOp &fusedOp : tiledAndFusedOps->fusedProducers) {
    ArrayRef<int64_t> fusedOpTileSizes = launchConfig.getTileSizes(fusedOp, 0);
    linalg::LinalgOp tiledOp = tileUnfusedLoops(
        builder, fusedOp, tiledAndFusedOps->fusedLoopDims, fusedOpTileSizes);
    if (!tiledOp) {
      return fusedOp.emitError("unable to tile unfused loops");
    }
  }
  linalg::LinalgOp tiledOp =
      tileUnfusedLoops(builder, tiledAndFusedOps->op,
                       tiledAndFusedOps->fusedLoopDims, tileSizes);
  if (!tiledOp) {
    return tiledAndFusedOps->op.emitError("unable to tile unfused loops");
  }

  applyCanonicalizationPatternsForTiling(context, funcOp);
  return success();
}

}  // namespace iree_compiler
}  // namespace mlir
