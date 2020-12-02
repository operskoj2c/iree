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

#include "iree/compiler/Conversion/LinalgToVector/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Utils/StructuredOpsUtils.h"
#include "mlir/Dialect/Vector/VectorOps.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define DEBUG_TYPE "iree-vectorize-conv"

namespace mlir {
namespace iree_compiler {

namespace {

/// Vectorizes linalg.conv for a single GPU invocation. Therefore, the
/// linalg.conv op should have a very specific form; other patterns are
/// expected to tile and distribute larger convolutions into this form for
/// a single GPU invocation.
///
/// The linalg.conv op should follow:
/// - Filter: HfWfCiCo format
/// - Input : NHiWiCi format
/// - Output: NHoWoCo format
/// - For output:
///   - N must be 1.
///   - Ho must be 1.
///   - Co must be a multiple of 4.
/// - For filter:
///   - Hf must be 1.
///   - Hf must be 1.
///   - Ci must be 4.
/// - No dilation.
/// - No padding.
///
/// Output channel is requried to be a multiple of 4 so that we can process
/// them with load4/store4, which is native to GPUs. Similarly for the input
/// channel size requirement.
struct VectorizeLinalgConv : OpRewritePattern<linalg::ConvOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(linalg::ConvOp convOp,
                                PatternRewriter &rewriter) const override {
    LLVM_DEBUG(llvm::dbgs() << "inspecting " << convOp << "\n");

    // This pattern does not handle convolutions with dilation.
    if (auto dilations = convOp.dilations()) {
      auto values = dilations->getAsValueRange<IntegerAttr>();
      if (llvm::any_of(values, [](const APInt &value) {
            return value.getSExtValue() != 1;
          })) {
        return failure();
      }
    }

    auto filterViewOp = convOp.filter().getDefiningOp<SubViewOp>();
    auto inputViewOp = convOp.input().getDefiningOp<SubViewOp>();
    auto outputViewOp = convOp.output().getDefiningOp<SubViewOp>();
    if (!filterViewOp || !inputViewOp || !outputViewOp) return failure();

    // The filter/input/output view should have static sizes to vectorize.
    if (!llvm::empty(filterViewOp.getDynamicSizes()) ||
        !llvm::empty(inputViewOp.getDynamicSizes()) ||
        !llvm::empty(outputViewOp.getDynamicSizes())) {
      return failure();
    }

    // The output batch and height dimensions should be 1. If not, other
    // patterns can generate parallel loops can distribute them.
    if (outputViewOp.getStaticSize(0) != 1 ||
        outputViewOp.getStaticSize(1) != 1) {
      return failure();
    }

    // We addtionally expect the filter height/width dimensions are both 1 to
    // simplify vectorization. Other patterns can generate loops to create 1x1
    // filter subivews.
    if (filterViewOp.getStaticSize(0) != 1 ||
        filterViewOp.getStaticSize(1) != 1) {
      return failure();
    }

    int64_t numInputChannels = filterViewOp.getStaticSize(2);
    int64_t numOutputChannels = filterViewOp.getStaticSize(3);
    if (numInputChannels != 4 || numOutputChannels % 4 != 0) return failure();

    int64_t numOutputWidths = outputViewOp.getStaticSize(2);
    int64_t widthStride = convOp.getStride(1);

    // This invocation handles a batch of (numOutputWidths * numOutputChannels).
    LLVM_DEBUG({
      llvm::dbgs() << "# output width: " << numOutputWidths << "\n";
      llvm::dbgs() << "# output channels: " << numOutputChannels << "\n";
      llvm::dbgs() << "width stride: " << widthStride << "\n";
    });

    MLIRContext *context = convOp.getContext();
    Location loc = convOp.getLoc();

    Type elementType = filterViewOp.getType().getElementType();
    auto filterVectorType =
        VectorType::get({numInputChannels, numOutputChannels}, elementType);
    auto vector1x4Type = VectorType::get({1, 4}, elementType);
    Value zero = rewriter.create<ConstantIndexOp>(loc, 0);

    // Load the entire filter subview.
    SmallVector<Value, 4> filterIndices(4, zero);
    Value wholeFilter = rewriter.create<vector::TransferReadOp>(
        loc, filterVectorType, filterViewOp, filterIndices);

    // Get filter slices so that later we can use them for dot product with the
    // input. Both the height and width dimensions are 1; so we just need to
    // loop over input and output channel dimensions.
    SmallVector<SmallVector<Value, 1>, 4> filterVectors(numInputChannels);
    for (int ic = 0; ic < numInputChannels; ++ic) {
      auto &thisInputChannel = filterVectors[ic];
      thisInputChannel.reserve(numOutputChannels / 4);
      for (int oc = 0; oc < numOutputChannels / 4; ++oc) {
        Value slice = rewriter.create<vector::ExtractStridedSliceOp>(
            loc, wholeFilter, /*offsets=*/ArrayRef<int64_t>({ic, oc * 4}),
            /*sizes=*/ArrayRef<int64_t>({1, 4}),
            /*strides=*/ArrayRef<int64_t>({1, 1}));
        thisInputChannel.push_back(slice);
      }
    }

    // Build indexing maps for a later vector contraction op.
    AffineExpr dim0 = getAffineDimExpr(0, context);  // M
    AffineExpr dim1 = getAffineDimExpr(1, context);  // N
    AffineExpr dim2 = getAffineDimExpr(2, context);  // K
    auto map02 = AffineMap::get(3, 0, {dim0, dim2}, context);
    auto map21 = AffineMap::get(3, 0, {dim2, dim1}, context);
    auto map01 = AffineMap::get(3, 0, {dim0, dim1}, context);
    ArrayAttr indexingMaps =
        rewriter.getAffineMapArrayAttr({map02, map21, map01});

    // Also build iterator types for the vector contraction op.
    ArrayAttr iterators = rewriter.getStrArrayAttr(
        {getParallelIteratorTypeName(), getParallelIteratorTypeName(),
         getReductionIteratorTypeName()});

    // Compute the (numOutputWidths * numOutputChannels) batch. We only
    // contribute numInputChannels accumulation along the reduction dimension.
    // So read in the result from the output, compose a chain of
    // numInputChannels vector dot operations, and then write out.
    for (int ow = 0; ow < numOutputWidths; ++ow) {
      // Read in the input vector for these 4 input channels a a batch. The
      // input vector are used for computing all output channels so data can
      // be reused.
      SmallVector<Value, 4> inputIndices(4, zero);
      inputIndices[2] = rewriter.create<ConstantIndexOp>(loc, ow * widthStride);
      Value inputVector = rewriter.create<vector::TransferReadOp>(
          loc, vector1x4Type, inputViewOp, inputIndices);

      for (int oc = 0; oc < numOutputChannels / 4; ++oc) {
        // Read in the initial value for this output vector.
        SmallVector<Value, 4> outputIndices(4, zero);
        outputIndices[2] = rewriter.create<ConstantIndexOp>(loc, ow);
        outputIndices[3] = rewriter.create<ConstantIndexOp>(loc, oc * 4);
        Value outputVector = rewriter.create<vector::TransferReadOp>(
            loc, vector1x4Type, outputViewOp, outputIndices);

        // Peform a chain of dot product and accumulation.
        for (int i = 0; i < numInputChannels; ++i) {
          auto inputSlice = rewriter.create<vector::ExtractStridedSliceOp>(
              loc, inputVector, /*offsets=*/ArrayRef<int64_t>({0, i}),
              /*sizes=*/ArrayRef<int64_t>({1, 1}),
              /*strides=*/ArrayRef<int64_t>({1, 1}));
          outputVector = rewriter.create<vector::ContractionOp>(
              loc, inputSlice, filterVectors[i][oc], outputVector, indexingMaps,
              iterators);
        }

        // Write out the output vector.
        rewriter.create<vector::TransferWriteOp>(loc, outputVector,
                                                 outputViewOp, outputIndices);
      }
    }

    rewriter.eraseOp(convOp);
    return success();
  }
};

struct VectorizeLinalgConvPass
    : public PassWrapper<VectorizeLinalgConvPass, OperationPass<FuncOp>> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<linalg::LinalgDialect, vector::VectorDialect>();
  }

  void runOnOperation() override {
    MLIRContext *context = &getContext();
    OwningRewritePatternList patterns;
    patterns.insert<VectorizeLinalgConv>(context);
    applyPatternsAndFoldGreedily(getOperation(), std::move(patterns));
  }
};

}  // namespace

void populateVectorizeLinalgConvPatterns(MLIRContext *context,
                                         OwningRewritePatternList &patterns) {
  patterns.insert<VectorizeLinalgConv>(context);
}

std::unique_ptr<Pass> createVectorizeLinalgConvPass() {
  return std::make_unique<VectorizeLinalgConvPass>();
}

static PassRegistration<VectorizeLinalgConvPass> pass(
    "iree-codegen-vectorize-linalg-conv",
    "Vectorize a very specific form of linalg.conv");

}  // namespace iree_compiler
}  // namespace mlir
