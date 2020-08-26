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

//===- ConvertToGPUPass.cpp -----------------------------------------------===//
//
// Partition computation within dispatch function to workgroups/workitems.
//
//===----------------------------------------------------------------------===//

#include <array>
#include <numeric>

#include "iree/compiler/Conversion/LinalgToSPIRV/Attributes.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/MarkerUtils.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/MemorySpace.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/Passes.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/Utils.h"
#include "mlir/Conversion/AffineToStandard/AffineToStandard.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SPIRV/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/TargetAndABI.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/FunctionSupport.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/LoopUtils.h"

namespace mlir {
namespace iree_compiler {

/// In some cases the iterations of the loops when partitioned to workgroups
/// need to be distributed in a cyclic manner. The main use case here is when
/// the number of workgroups is constrained such that the number of iterations
/// is greater than or equal to number of processors (along any dimension). In
/// those cases, distribute the iterations in a cyclic manner. This adds
/// additional control flow, but isn't too detrimental to performance since they
/// are convergent for the most part.
static llvm::cl::opt<bool> isWorkgroupCountConstrained(
    "iree-codegen-constrained-workgroup-count",
    llvm::cl::desc("Specify whether the number of workgroups can be assumed to "
                   "be large enough to cover the entire workload"),
    llvm::cl::init(false));

//===----------------------------------------------------------------------===//
// Loop utilities
//===----------------------------------------------------------------------===//

/// Builds an empty scf.for operation. The default builder adds an entry basic
/// block which needs to be avoided here.
static scf::ForOp buildEmptyForOp(Location loc, OpBuilder &builder, Value lb,
                                  Value ub, Value step) {
  OperationState state(loc, scf::ForOp::getOperationName());
  state.addOperands({lb, ub, step});
  state.addRegion();
  return cast<scf::ForOp>(builder.createOperation(state));
}

/// Builds an empty scf.if operation without the then and else blocks.
static scf::IfOp buildEmptyIfOp(Location loc, OpBuilder &builder, Value cond) {
  OperationState state(loc, scf::IfOp::getOperationName());
  state.addOperands(cond);
  state.addRegion();
  state.addRegion();
  return cast<scf::IfOp>(builder.createOperation(state));
}

namespace {
struct LoopBounds {
  Value lb;
  Value ub;
  Value step;
};
}  // namespace

/// Replaces a scf.parallelOp with an optional scf.parallel op and nested
/// scf.for operations. To create the scf.parallel op as the outermost loop,
/// pass the lower bound, upper bound and steps in `newPLoopLbs`, `newPLoopUbs`,
/// and `newPLoopStep` respectively. The bounds of the inner scf.for operations
/// to be created are passed in `forLbs`, `forUbs`, and `forStep`. The
/// `permutation` vector contains a mapping from the original loop order, to the
/// loop order to be generated.
static Operation *replacePLoopOp(ConversionPatternRewriter &rewriter,
                                 scf::ParallelOp pLoopOp,
                                 ArrayRef<LoopBounds> newPLoopBounds,
                                 ArrayRef<LoopBounds> forBounds,
                                 ArrayRef<unsigned> permutation) {
  assert(!forBounds.empty() && "unhandled case of no scf.for created");
  unsigned numLoops = pLoopOp.getNumLoops();
  Location loc = pLoopOp.getLoc();
  assert(forBounds.size() + newPLoopBounds.size() == numLoops &&
         "cannot drop loops when splitting scf.parallel operation");
  assert(permutation.size() == numLoops);
  OpBuilder::InsertionGuard guard(rewriter);

  // Need a signature conversion for the body of the scf.parallel operation,
  // before can it can be used as the body of the innermost loop created here.
  TypeConverter::SignatureConversion signatureConverter(numLoops);
  Operation *outermostLoop = nullptr;
  auto permuteIt = permutation.begin();

  // Create the scf.parallel operation as the outermost loop, if specified.
  if (!newPLoopBounds.empty()) {
    auto lbs = llvm::to_vector<2>(llvm::map_range(
        newPLoopBounds, [](LoopBounds bounds) -> Value { return bounds.lb; }));
    auto ubs = llvm::to_vector<2>(llvm::map_range(
        newPLoopBounds, [](LoopBounds bounds) { return bounds.ub; }));
    auto steps = llvm::to_vector<2>(llvm::map_range(
        newPLoopBounds, [](LoopBounds bounds) { return bounds.step; }));
    auto newPLoop = rewriter.create<scf::ParallelOp>(loc, lbs, ubs, steps);
    for (auto iv : newPLoop.getInductionVars()) {
      signatureConverter.remapInput(*permuteIt, iv);
      permuteIt++;
    }
    rewriter.setInsertionPointToStart(newPLoop.getBody());
    outermostLoop = newPLoop.getOperation();
  }

  // Generate the nested scf.for operations with the bounds passed.
  for (auto it : enumerate(forBounds)) {
    Value lb = it.value().lb, ub = it.value().ub, step = it.value().step;
    if (it.index() != forBounds.size() - 1) {
      auto forOp = rewriter.create<scf::ForOp>(loc, lb, ub, step);
      if (!outermostLoop) outermostLoop = forOp.getOperation();
      signatureConverter.remapInput(*permuteIt, forOp.getInductionVar());
      rewriter.setInsertionPointToStart(forOp.getBody());
    } else {
      // For the last loop, move the body of the scf.parallel op as the body of
      // the loop after signature conversion.
      auto forOp = buildEmptyForOp(loc, rewriter, lb, ub, step);
      if (!outermostLoop) outermostLoop = forOp.getOperation();
      signatureConverter.addInputs(*permuteIt, rewriter.getIndexType());
      Region &pLoopOpRegion = pLoopOp.getLoopBody();
      rewriter.applySignatureConversion(&pLoopOpRegion, signatureConverter);
      Region &forOpRegion = forOp.getLoopBody();
      rewriter.inlineRegionBefore(pLoopOpRegion, forOpRegion,
                                  forOpRegion.begin());
    }
    permuteIt++;
  }
  rewriter.eraseOp(pLoopOp);
  return outermostLoop;
}

/// Serializes the dimensions of the scf.parallel specified in
/// `serializedDimensions`, by creating an nested scf.for operation for each
/// dimension.
// TODO(ravishankarm): Move this into LoopUtils.h in MLIR.
static Operation *serializeDimensions(ConversionPatternRewriter &rewriter,
                                      scf::ParallelOp pLoopOp,
                                      ArrayRef<unsigned> serializedDimensions) {
  assert(!serializedDimensions.empty() &&
         "unhandled corner case of no serializing dims");
  OpBuilder::InsertionGuard guard(rewriter);
  DenseSet<unsigned> serializedDimSet;
  serializedDimSet.insert(serializedDimensions.begin(),
                          serializedDimensions.end());
  assert(serializedDimSet.size() == serializedDimensions.size() &&
         "cannot repeat dimensions during serialization of scf.parallel");
  SmallVector<LoopBounds, 2> newPLoopBounds, forBounds;
  SmallVector<unsigned, 2> permutation;
  auto lbs = pLoopOp.lowerBound();
  auto ubs = pLoopOp.upperBound();
  auto steps = pLoopOp.step();
  for (unsigned i : llvm::seq<unsigned>(0, pLoopOp.getNumLoops())) {
    if (serializedDimSet.count(i)) {
      forBounds.push_back({lbs[i], ubs[i], steps[i]});
    } else {
      newPLoopBounds.push_back({lbs[i], ubs[i], steps[i]});
      permutation.push_back(i);
    }
  }
  permutation.append(serializedDimensions.begin(), serializedDimensions.end());
  return replacePLoopOp(rewriter, pLoopOp, newPLoopBounds, forBounds,
                        permutation);
}

/// Serialize all inner dimensions of a `pLoopOp` starting from `serializeFrom`.
static Operation *serializeDimensionsFrom(ConversionPatternRewriter &rewriter,
                                          scf::ParallelOp pLoopOp,
                                          unsigned serializeFrom) {
  unsigned numLoops = pLoopOp.getNumLoops();
  assert(serializeFrom < numLoops &&
         "unhandled corner case of no serialization");
  SmallVector<unsigned, 2> serializedDimensions;
  for (unsigned dim : llvm::seq(serializeFrom, numLoops))
    serializedDimensions.push_back(dim);
  return serializeDimensions(rewriter, pLoopOp, serializedDimensions);
}

/// Collapses all loops in a scf.parallel into one scf.parallel operation. This
/// is done by
/// 1) Normalize the loop bounds to be [0, (ub - lb) / step)
/// 2) Compute the total number of iterations.
/// 3) From the induction variable of the modified loop, compute the values of
///    the original induction variables by de-linearization.
scf::ParallelOp collapseParallelLoops(ConversionPatternRewriter &rewriter,
                                      scf::ParallelOp pLoopOp) {
  if (pLoopOp.getNumReductions()) return nullptr;

  unsigned numLoops = pLoopOp.getNumLoops();
  if (numLoops == 1) return pLoopOp;

  // Compute the number of iterations of each loops starting from the innermost.
  Location loc = pLoopOp.getLoc();
  Value totalNumIterations = rewriter.create<ConstantIndexOp>(loc, 1);

  // Track the "stride" of each loop, i.e. product of the total number of
  // iterations of the inner loops.
  SmallVector<Value, 2> iterationStride;
  iterationStride.resize(pLoopOp.getNumLoops());
  auto lbs = pLoopOp.lowerBound();
  auto ubs = pLoopOp.upperBound();
  auto steps = pLoopOp.step();
  for (int i = numLoops - 1; i >= 0; --i) {
    Value lb = lbs[i], ub = ubs[i], step = steps[i];
    Value iterCount = rewriter.create<SignedDivIOp>(
        loc, rewriter.create<SubIOp>(loc, ub, lb), step);
    iterationStride[i] = totalNumIterations;
    totalNumIterations =
        rewriter.create<MulIOp>(loc, totalNumIterations, iterCount);
  }

  // Create the collapsed parallel loop op with lowerbound 0, step 1 and upper
  // bound being the totalNumIterations.
  Value newLb = rewriter.create<ConstantIndexOp>(loc, 0);
  Value newStep = rewriter.create<ConstantIndexOp>(loc, 1);
  scf::ParallelOp newPLoopOp =
      rewriter.create<scf::ParallelOp>(loc, newLb, totalNumIterations, newStep);

  // Build the body of the collapsed loop by cloning the original loop body. The
  // replacement value of the induction variables of the original loop body,
  // from the induction variable of the new loop, using
  //   origLoopIv[i] = loopIv / iterationStride[i]
  //   loopIv = loopIv % iterationStride[i]
  OpBuilder::InsertionGuard guard(rewriter);
  Block &pLoopBody = pLoopOp.getLoopBody().front();
  rewriter.setInsertionPointToStart(&newPLoopOp.getLoopBody().front());
  Value loopIv = *newPLoopOp.getInductionVars().begin();
  BlockAndValueMapping map;
  edsc::ScopedContext scope(rewriter, loc);
  using namespace edsc::op;
  for (int i : llvm::seq<int>(0, numLoops)) {
    Value iterNum =
        rewriter.create<SignedDivIOp>(loc, loopIv, iterationStride[i]);
    Value newIv = lbs[i] + (iterNum * steps[i]);
    map.map(pLoopBody.getArgument(i), newIv);
    loopIv = rewriter.create<SignedRemIOp>(loc, loopIv, iterationStride[i]);
  }
  for (Operation &op : pLoopBody.without_terminator()) {
    rewriter.clone(op, map);
  }
  rewriter.eraseOp(pLoopOp);
  return newPLoopOp;
}

//===----------------------------------------------------------------------===//
// GPU processor ID mapping utilities
//===----------------------------------------------------------------------===//

/// Distributes scf.parallel to processors with the processors logically
/// arranged with same dimensionality as the number of loops, i.e. a
/// scf.parallel with 2 loops to a 2D grid of processors. `processorIDs` and
/// `numProcessors` must be of same size as the number of loops and are the
/// values to use for process ID and number of processors along each dimension
/// in the distributed code.
/// This method accounts for the case where the number of processors is not
/// enough to execute the entire iteration space with one iteration mapped to
/// each processor. So implements a cyclic distribution of iterations to
/// processors.
static LogicalResult distributeCyclicallyToProcessors(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp,
    ArrayRef<linalg::ProcInfo> procInfo) {
  unsigned numLoops = pLoopOp.getNumLoops();
  assert(numLoops == procInfo.size() &&
         "expected as many ids as number of loops");
  SmallVector<LoopBounds, 2> forBounds;
  SmallVector<unsigned, 2> permutation;
  forBounds.reserve(numLoops);
  permutation.reserve(numLoops);
  Location loc = pLoopOp.getLoc();
  auto lbs = pLoopOp.lowerBound(), ubs = pLoopOp.upperBound(),
       steps = pLoopOp.step();
  for (unsigned i : llvm::seq<unsigned>(0, procInfo.size())) {
    Value mappedLb = rewriter.create<AddIOp>(
        loc, lbs[i],
        rewriter.create<MulIOp>(loc, steps[i], procInfo[i].procId));
    Value mappedStep =
        rewriter.create<MulIOp>(loc, steps[i], procInfo[i].nprocs);
    forBounds.push_back({mappedLb, ubs[i], mappedStep});
    permutation.push_back(i);
  }
  replacePLoopOp(rewriter, pLoopOp, /*newPLoopBounds=*/{}, forBounds,
                 permutation);
  return success();
}

/// Distributes scf.parallel to processors with the processors logically
/// arranged with same dimensionality as the number of loops, i.e. a
/// scf.parallel with 2 loops to a 2D grid of processors. `processorIDs` must be
/// of same size as the number of loops and are the values to use for process ID
/// and number of processors along each dimension in the distributed code.  This
/// method assumes that the number of processors is greater than or equal to the
/// number of iterations. So just generates an if statement to mask of
/// processors with no work. When the number of processors is known to be
/// exactly equal to the number of iterations, the if statement is not needed as
/// well. In such cases, `generateGuard` can be set to `false` to avoid
/// generating the if statement.
static LogicalResult distributeSingleIterationPerProcessor(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp,
    ArrayRef<linalg::ProcInfo> procInfo, bool generateGuard = false) {
  unsigned numLoops = pLoopOp.getNumLoops();
  Location loc = pLoopOp.getLoc();
  assert(numLoops == procInfo.size() &&
         "expected as many ids as number of loops");

  auto lbs = pLoopOp.lowerBound();
  auto step = pLoopOp.step();
  SmallVector<Value, 2> ivReplacements;
  for (unsigned i : llvm::seq<unsigned>(0, numLoops)) {
    Value iterValue = rewriter.create<AddIOp>(
        loc, lbs[i], rewriter.create<MulIOp>(loc, procInfo[i].procId, step[i]));
    ivReplacements.push_back(iterValue);
  }
  Region &pLoopOpRegion = pLoopOp.getLoopBody();

  if (generateGuard) {
    TypeConverter::SignatureConversion signatureConverter(numLoops);
    Value cond = nullptr;
    auto ubs = pLoopOp.upperBound();
    for (unsigned i : llvm::seq<unsigned>(0, numLoops)) {
      Value cmp = rewriter.create<CmpIOp>(loc, CmpIPredicate::slt,
                                          ivReplacements[i], ubs[i]);
      cond = (cond ? rewriter.create<AndOp>(loc, cond, cmp) : cmp);
      signatureConverter.remapInput(i, ivReplacements[i]);
    }
    rewriter.applySignatureConversion(&pLoopOpRegion, signatureConverter);
    scf::IfOp ifOp = buildEmptyIfOp(loc, rewriter, cond);
    Region &ifOpRegion = ifOp.getRegion(0);
    rewriter.inlineRegionBefore(pLoopOpRegion, ifOpRegion, ifOpRegion.begin());
  } else {
    // The body of the scf.parallel needs to be moved into its parent
    // operation.
    // - Split the block just before the scf.parallel operation.
    // - Move the only block of scf.parallel before the newly created block
    //   (after signature conversion).
    // - Add branch from the original block to the moved block of the
    //   scf.parallel's region, and from the latter to the block created by the
    //   split operation.
    // - Canonicalization will fold these branches away.
    Block *destBlock = pLoopOp.getOperation()->getBlock();
    Block *remainingInst =
        rewriter.splitBlock(destBlock, Block::iterator(pLoopOp));
    Block *sourceBlock = &pLoopOpRegion.front();
    rewriter.eraseOp(sourceBlock->getTerminator());
    rewriter.mergeBlocks(&pLoopOpRegion.front(), destBlock, ivReplacements);
    rewriter.mergeBlocks(remainingInst, destBlock, {});
  }
  rewriter.eraseOp(pLoopOp);
  return success();
}

template <typename GPUIdOp, typename GPUCountOp>
static linalg::ProcInfo getLinearizedGPUProcessorIdAndCount(
    Location loc, ConversionPatternRewriter &rewriter) {
  SmallVector<linalg::ProcInfo, 3> procInfo =
      getGPUProcessorIdsAndCounts<GPUIdOp, GPUCountOp>(rewriter, loc,
                                                       kNumGPUDims);
  linalg::ProcInfo linearized;
  linearized.procId = procInfo[0].procId;
  linearized.nprocs = procInfo[0].nprocs;
  for (unsigned i = 0; i < kNumGPUDims - 1; ++i) {
    linearized.procId =
        rewriter.create<MulIOp>(loc, linearized.procId, procInfo[i + 1].nprocs);
    linearized.procId =
        rewriter.create<AddIOp>(loc, linearized.procId, procInfo[i + 1].procId);
    linearized.nprocs =
        rewriter.create<MulIOp>(loc, linearized.nprocs, procInfo[i + 1].nprocs);
  }
  return linearized;
}

/// Distributes scf.parallel to processors where `IdOp` is used to get the
/// processor ID and `DimOp` is used to get the number of processors along a
/// dimension.
template <typename GPUIdOp, typename GPUCountOp>
static LogicalResult distributeCyclicallyToProcessors(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp) {
  unsigned numLoops = pLoopOp.getNumLoops();
  if (numLoops > 3) {
    pLoopOp =
        cast<scf::ParallelOp>(serializeDimensionsFrom(rewriter, pLoopOp, 3));
    numLoops = 3;
  }
  SmallVector<linalg::ProcInfo, 2> procInfo =
      getGPUProcessorIdsAndCounts<GPUIdOp, GPUCountOp>(
          rewriter, pLoopOp.getLoc(), numLoops);
  return distributeCyclicallyToProcessors(rewriter, pLoopOp, procInfo);
}

/// Distributes scf.parallel to processors where `IdOp` is used to get the
/// processor ID and `DimOp` is used to get the number of processors along a
/// dimension. Assumes that the number of processors will be less than equal to
/// the number of iterations of the pLoopOp along all dimensions.
template <typename GPUIdOp, typename GPUCountOp>
static LogicalResult distributeSingleIterationPerProcessor(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp,
    bool generateGuard = true) {
  unsigned numLoops = pLoopOp.getNumLoops();
  if (numLoops > 3) {
    pLoopOp =
        cast<scf::ParallelOp>(serializeDimensionsFrom(rewriter, pLoopOp, 3));
    numLoops = 3;
  }
  auto procInfo = getGPUProcessorIdsAndCounts<GPUIdOp, GPUCountOp>(
      rewriter, pLoopOp.getLoc(), numLoops);
  return distributeSingleIterationPerProcessor(rewriter, pLoopOp, procInfo,
                                               generateGuard);
}

/// Distribute the scf.parallel to workgroups.
static LogicalResult mapToWorkgroups(ConversionPatternRewriter &rewriter,
                                     scf::ParallelOp pLoopOp,
                                     bool useCyclicDistribution = false) {
  if (useCyclicDistribution) {
    return distributeCyclicallyToProcessors<gpu::BlockIdOp, gpu::GridDimOp>(
        rewriter, pLoopOp);
  }
  return distributeSingleIterationPerProcessor<gpu::BlockIdOp, gpu::GridDimOp>(
      rewriter, pLoopOp, false);
}

/// Distributes scf.parallel to workitems using local invocation ID.
static LogicalResult mapToLocalInvocationId(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp,
    bool useCyclicDistribution = false) {
  if (useCyclicDistribution) {
    return distributeCyclicallyToProcessors<gpu::ThreadIdOp, gpu::BlockDimOp>(
        rewriter, pLoopOp);
  }
  return distributeSingleIterationPerProcessor<gpu::ThreadIdOp,
                                               gpu::BlockDimOp>(rewriter,
                                                                pLoopOp);
}

/// Distributes scf.parallel to workitems using global invocation ID. The GPU
/// dialect doesn't have a direct operation to do this. This could be done using
/// id = blockIdx * blockDim + gridIdx. count = blockDim * gridDim.
static LogicalResult mapToGlobalInvocationId(
    ConversionPatternRewriter &rewriter, scf::ParallelOp pLoopOp) {
  return distributeSingleIterationPerProcessor<GPUGlobalId, GPUGlobalCount>(
      rewriter, pLoopOp);
}

/// Returns the number of bytes copied when loading to/storing from workgorup
/// memory. It is approximated to be the size of the underlying allocation being
/// copied into/from.
static Optional<int64_t> getLinearizedCopySize(linalg::CopyOp copyOp) {
  Value src = copyOp.input();
  Value dst = copyOp.output();
  MemRefType srcType = src.getType().cast<MemRefType>();
  MemRefType dstType = dst.getType().cast<MemRefType>();

  Value workgroupMemoryView;
  MemRefType workgroupMemoryType;
  if (srcType.getMemorySpace() == getWorkgroupMemorySpace()) {
    workgroupMemoryView = src;
    workgroupMemoryType = srcType;
  } else if (dstType.getMemorySpace() == getWorkgroupMemorySpace()) {
    workgroupMemoryView = dst;
    workgroupMemoryType = dstType;
  } else {
    return {};
  }

  SubViewOp workgroupMemorySubviewOp =
      dyn_cast_or_null<SubViewOp>(workgroupMemoryView.getDefiningOp());
  if (!workgroupMemorySubviewOp) return {};
  AllocOp allocOp = dyn_cast_or_null<AllocOp>(
      workgroupMemorySubviewOp.source().getDefiningOp());
  if (!allocOp) return {};

  MemRefType allocOpType = allocOp.getType();
  if (!allocOpType.hasStaticShape()) return {};
  return allocOpType.getNumElements();
}

//===----------------------------------------------------------------------===//
// Pass and patterns.
//===----------------------------------------------------------------------===//

namespace {
/// Pass to convert from tiled and fused linalg ops into gpu.func.
struct ConvertToGPUPass : public PassWrapper<ConvertToGPUPass, FunctionPass> {
  ConvertToGPUPass() = default;
  ConvertToGPUPass(const ConvertToGPUPass &pass) {}

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<gpu::GPUDialect>();
  }

  void runOnFunction() override;
};

struct SerializeParallelLoopPattern
    : public OpConversionPattern<scf::ParallelOp> {
  using OpConversionPattern<scf::ParallelOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      scf::ParallelOp pLoopOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    return success(serializeDimensionsFrom(rewriter, pLoopOp, 0) != nullptr);
  }
};

/// Implementation of the mapping of tiled linalg op to workitems within a
/// workgroup.
template <typename LinalgOpTy>
static LogicalResult mapLinalgOpToLocalInvocationIdImpl(
    LinalgOpTy linalgOp, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) {
  // Check for marker that specifies that the linalg op is to be partitioned
  // across threads within a workgroup.
  if (!hasMarker(linalgOp)) return failure();
  Optional<linalg::LinalgLoops> loops =
      linalg::linalgLowerOpToLoops<scf::ParallelOp>(rewriter, linalgOp);
  if (!loops) return failure();
  if (loops.getValue().empty()) return success();

  auto pLoopOp = cast<scf::ParallelOp>(loops.getValue()[0]);
  return mapToLocalInvocationId(
      rewriter, pLoopOp,
      hasMarker(linalgOp, {getWorkgroupMarker(), getWorkgroupMemoryMarker()}));
}

static LogicalResult distributeCopyOp(linalg::CopyOp copyOp,
                                      scf::ParallelOp pLoopOp,
                                      ConversionPatternRewriter &rewriter) {
  pLoopOp = collapseParallelLoops(rewriter, pLoopOp);
  if (!pLoopOp) return failure();

  Optional<int64_t> copyLength = getLinearizedCopySize(copyOp);
  linalg::ProcInfo idAndCount =
      getLinearizedGPUProcessorIdAndCount<gpu::ThreadIdOp, gpu::BlockDimOp>(
          copyOp.getLoc(), rewriter);
  auto workgroupSize =
      spirv::lookupLocalWorkGroupSize(copyOp).getValues<APInt>();
  int64_t linearizedWorkgroupSize = std::accumulate(
      workgroupSize.begin(), workgroupSize.end(), 1,
      [](int64_t total, APInt value) { return total * value.getSExtValue(); });

  if (copyLength.hasValue() && !workgroupSize.empty() &&
      copyLength.getValue() <= linearizedWorkgroupSize) {
    return distributeSingleIterationPerProcessor(rewriter, pLoopOp, idAndCount,
                                                 /*generateGuard=*/true);
  }
  return distributeCyclicallyToProcessors(rewriter, pLoopOp, idAndCount);
}

/// CopyOp that are loading to/storing from workgroup memory are special cased
/// to use all workitems to do a copy. This is done by linearizing the copy
/// operation.
// TODO(ravishankarm): This linearization is achieved through collapsing the
// generated parallel loops from a multi-dimensional copy. Such lowering results
// in mods/divs in the collapsed loop body. This can be removed by reshaping the
// copy to be a 1D copy. This seems to be hitting an error in reshape
// canonicalization. Investigate this further.
template <>
LogicalResult mapLinalgOpToLocalInvocationIdImpl<linalg::CopyOp>(
    linalg::CopyOp copyOp, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) {
  if (!hasMarker(copyOp, getCopyToWorkgroupMemoryMarker())) return failure();
  Optional<linalg::LinalgLoops> loops =
      linalg::linalgLowerOpToLoops<scf::ParallelOp>(rewriter, copyOp);
  if (!loops) return failure();
  if (loops.getValue().empty()) return success();

  auto pLoopOp = cast<scf::ParallelOp>(loops.getValue()[0]);
  return distributeCopyOp(copyOp, pLoopOp, rewriter);
}

/// Map tiled linalg op to workitems by lowering it to scf.parallel and
/// partitioning it to workitems.
template <typename LinalgOpTy>
struct MapLinalgOpToLocalInvocationId : public OpConversionPattern<LinalgOpTy> {
  using OpConversionPattern<LinalgOpTy>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      LinalgOpTy linalgOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    if (failed(
            mapLinalgOpToLocalInvocationIdImpl(linalgOp, operands, rewriter)))
      return failure();

    // If the `linalgOp` writes to workgroup memory insert barrier after the
    // op.
    if (llvm::any_of(linalgOp.getOperands(), [](Value output) {
          return output.getType().cast<MemRefType>().getMemorySpace() ==
                 getWorkgroupMemorySpace();
        })) {
      rewriter.create<spirv::ControlBarrierOp>(
          linalgOp.getLoc(), spirv::Scope::Workgroup, spirv::Scope::Workgroup,
          spirv::MemorySemantics::AcquireRelease);
    }
    rewriter.eraseOp(linalgOp);
    return success();
  }
};

/// Map linalg operation to execute on GPU in parallel by mapping the parallel
/// loops to "GlobalInvocationId".
template <typename LinalgOpTy>
struct MapLinalgOpToGlobalInvocationId
    : public OpConversionPattern<LinalgOpTy> {
  using OpConversionPattern<LinalgOpTy>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      LinalgOpTy linalgOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    // If marker exists do nothing.
    if (hasMarker(linalgOp)) return failure();
    FuncOp funcOp = linalgOp.template getParentOfType<FuncOp>();
    if (!funcOp) return failure();
    Optional<linalg::LinalgLoops> loops =
        linalg::linalgLowerOpToLoops<scf::ParallelOp>(rewriter, linalgOp);
    if (!loops) return failure();

    SmallVector<int64_t, 3> workgroupSize(3, 1);
    if (!loops.getValue().empty()) {
      scf::ParallelOp pLoopOp = dyn_cast<scf::ParallelOp>(loops.getValue()[0]);
      // If there are parallel loops partition them to threads using global
      // invocation ID.
      if (pLoopOp) {
        pLoopOp = collapseParallelLoops(rewriter, pLoopOp);
        if (!pLoopOp) return failure();
        if (failed(mapToGlobalInvocationId(rewriter, pLoopOp)))
          return rewriter.notifyMatchFailure(
              linalgOp, "mapping to GlobalInvocationID failed");
        workgroupSize = {32, 1, 1};
      }
    }
    rewriter.eraseOp(linalgOp);
    if (failed(updateWorkGroupSize(funcOp, workgroupSize))) return failure();
    funcOp.setAttr(getWorkgroupCountAttrName(),
                   rewriter.getI32IntegerAttr(static_cast<int32_t>(
                       WorkgroupCountMethodology::LinearizeResultShape)));
    return success();
  }
};

/// Remove the linalg.range operation created when lowering to loops.
struct RemoveLinalgRange : public OpConversionPattern<linalg::RangeOp> {
  using OpConversionPattern<linalg::RangeOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      linalg::RangeOp rangeOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    if (!rangeOp.getResult().use_empty()) return failure();
    rewriter.eraseOp(rangeOp);
    return success();
  }
};
}  // namespace

// Applies tiling followed to load/store optimized size then distribute on
// incovations.
static LogicalResult linalgCopyTileAndDistribute(
    linalg::CopyOp copyOp, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) {
  linalg::LinalgTilingOptions options;
  // Tile to memory access of 128bits as those tend to be optimal on most GPUs.
  constexpr unsigned vecLoadBits = 128;
  unsigned elementBits =
      copyOp.getSource().getType().cast<MemRefType>().getElementTypeBitWidth();
  if (elementBits == 0 || vecLoadBits % elementBits != 0) return failure();
  unsigned numElement = vecLoadBits / elementBits;
  options.setTileSizes({1, numElement})
      .setLoopType(linalg::LinalgTilingLoopType::ParallelLoops);
  Optional<linalg::TiledLinalgOp> tiledOp = linalg::tileLinalgOp(
      rewriter, cast<linalg::LinalgOp>(copyOp.getOperation()), options);
  if (!tiledOp) return failure();
  if (tiledOp->loops.empty()) return success();
  setMarker(tiledOp->op, getVectorizeMarker());
  auto pLoopOp = cast<scf::ParallelOp>(tiledOp->loops[0]);
  return distributeCopyOp(copyOp, pLoopOp, rewriter);
}

namespace {
// Pattern to tile and distribute linalg::CopyOp.
struct TileAndDistributeCopyOp : public OpConversionPattern<linalg::CopyOp> {
  using OpConversionPattern<linalg::CopyOp>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      linalg::CopyOp linalgOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    if (!hasMarker(linalgOp, getCopyToWorkgroupMemoryMarker())) {
      return failure();
    }
    if (failed(linalgCopyTileAndDistribute(linalgOp, operands, rewriter))) {
      return failure();
    }

    // Insert a barrier if read or write shared memory.
    if (llvm::any_of(linalgOp.getOperands(), [](Value output) {
          return output.getType().cast<MemRefType>().getMemorySpace() ==
                 getWorkgroupMemorySpace();
        })) {
      rewriter.create<spirv::ControlBarrierOp>(
          linalgOp.getLoc(), spirv::Scope::Workgroup, spirv::Scope::Workgroup,
          spirv::MemorySemantics::AcquireRelease);
    }
    rewriter.eraseOp(linalgOp);
    return success();
  }
};
}  // namespace

void populateLinalgTileAndDistributePatterns(
    MLIRContext *context, OwningRewritePatternList &patterns) {
  patterns.insert<TileAndDistributeCopyOp>(context);
}

void ConvertToGPUPass::runOnFunction() {
  FuncOp funcOp = getFunction();

  Region &body = funcOp.getBody();
  if (!llvm::hasSingleElement(body)) {
    funcOp.emitError("unhandled dispatch function with multiple blocks");
    return signalPassFailure();
  }

  MLIRContext *context = &getContext();
  ConversionTarget target(*context);
  // After this pass Linalg and scf.parallel ops should be gone.
  target.addIllegalOp<scf::ParallelOp>();
  target.addIllegalDialect<linalg::LinalgDialect>();
  // Reshape ops are treated legal since they just change the way the underlying
  // buffer is viewed. These are legalized downstream. They become no ops when
  // lowering to SPIR-V since the SPIR-V code uses linearized arrays.
  target.addLegalOp<linalg::ReshapeOp>();
  // Let the rest fall through.
  target.markUnknownOpDynamicallyLegal([](Operation *) { return true; });

  OwningRewritePatternList patterns;

  patterns.insert<MapLinalgOpToGlobalInvocationId<linalg::CopyOp>,
                  MapLinalgOpToGlobalInvocationId<linalg::FillOp>,
                  MapLinalgOpToGlobalInvocationId<linalg::GenericOp>,
                  MapLinalgOpToGlobalInvocationId<linalg::IndexedGenericOp>,
                  MapLinalgOpToLocalInvocationId<linalg::ConvOp>,
                  MapLinalgOpToLocalInvocationId<linalg::CopyOp>,
                  MapLinalgOpToLocalInvocationId<linalg::MatmulOp>,
                  MapLinalgOpToLocalInvocationId<linalg::BatchMatmulOp>,
                  MapLinalgOpToLocalInvocationId<linalg::PoolingMaxOp>,
                  MapLinalgOpToLocalInvocationId<linalg::PoolingMinOp>,
                  MapLinalgOpToLocalInvocationId<linalg::PoolingSumOp>,
                  RemoveLinalgRange, SerializeParallelLoopPattern>(context);

  if (failed(applyFullConversion(funcOp, target, patterns)))
    return signalPassFailure();
}

std::unique_ptr<OperationPass<FuncOp>> createConvertToGPUPass() {
  return std::make_unique<ConvertToGPUPass>();
}

static PassRegistration<ConvertToGPUPass> pass(
    "iree-codegen-convert-to-gpu", "Map tiled linalg and loop ops to GPU",
    [] { return std::make_unique<ConvertToGPUPass>(); });

}  // namespace iree_compiler
}  // namespace mlir
