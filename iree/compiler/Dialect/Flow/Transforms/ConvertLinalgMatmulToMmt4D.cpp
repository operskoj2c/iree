// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>

#include "iree/compiler/Dialect/Flow/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

namespace {

// Expands a 2D tensor input to a 4D tensor representing the same underlying
// data but now in a tiled layout, given a static 2D tile shape.
// Does not transpose.
// Example: (M, N) --> (M1, M0, N1, N0)
static Value expandTo4D(mlir::Location loc, PatternRewriter &rewriter,
                        Value input, ArrayRef<int64_t> tileShape) {
  RankedTensorType inputType = input.getType().cast<RankedTensorType>();
  ArrayRef<int64_t> inputShape = inputType.getShape();
  std::array<int64_t, 4> targetShape;
  // Generate a 4D shape of the form (M1, M0, N1, N0),
  // where M0, N0 are always static and M1, N1 are static if and only if M, N
  // are.
  for (int i : {0, 1}) {
    if (inputShape[i] == ShapedType::kDynamicSize) {
      targetShape[2 * i] = ShapedType::kDynamicSize;
    } else {
      targetShape[2 * i] = inputShape[i] / tileShape[i];
    }
    targetShape[2 * i + 1] = tileShape[i];
  }
  RankedTensorType targetType =
      RankedTensorType::get(targetShape, inputType.getElementType());
  std::array<ReassociationIndices, 2> expandIndices = {
      ReassociationIndices{0, 1}, ReassociationIndices{2, 3}};
  Value reshapedOperand = rewriter.create<linalg::TensorExpandShapeOp>(
      loc, targetType, input, expandIndices);
  return reshapedOperand;
}

// Creates a linalg.generic that transposes input using permutation indices.
// Example: (M1, M0, N1, N0) -> (M1, N1, M0, N0) if indices = {0, 2, 1, 3}.
static Value transpose(mlir::Location loc, PatternRewriter &rewriter,
                       Value input, ArrayRef<int64_t> indices) {
  RankedTensorType inputType = input.getType().cast<RankedTensorType>();
  auto nloops = indices.size();

  // TODO: use AffineMap::getPermutationMap?
  SmallVector<AffineExpr, 4> exprs = llvm::to_vector<4>(
      llvm::map_range(indices, [&](int64_t index) -> AffineExpr {
        return rewriter.getAffineDimExpr(index);
      }));

  ArrayRef<int64_t> inputShape = inputType.getShape();
  SmallVector<OpFoldResult, 4> targetShape;
  for (int i = 0; i < 4; i++) {
    if (inputShape[indices[i]] == ShapedType::kDynamicSize) {
      targetShape.emplace_back(
          rewriter.create<tensor::DimOp>(loc, input, indices[i]));
    } else {
      targetShape.push_back(rewriter.getIndexAttr(inputShape[indices[i]]));
    }
  }

  Value outputTensor = rewriter.create<linalg::InitTensorOp>(
      loc, targetShape, inputType.getElementType());

  SmallVector<StringRef, 4> loopAttributeTypes(nloops, "parallel");

  SmallVector<AffineMap, 2> indexingMaps = {
      inversePermutation(
          AffineMap::get(nloops, 0, exprs, rewriter.getContext())),
      AffineMap::getMultiDimIdentityMap(nloops, rewriter.getContext())};

  auto transposedOp = rewriter.create<linalg::GenericOp>(
      loc, outputTensor.getType(),
      /*inputs=*/input, /*outputs=*/outputTensor, indexingMaps,
      loopAttributeTypes,
      [&](OpBuilder &nestedBuilder, Location nestedLoc, ValueRange args) {
        nestedBuilder.create<linalg::YieldOp>(nestedLoc, args[0]);
      });

  return transposedOp.getResult(0);
};

// Collapses a 4d tensor input to 2d given its target shape.
// Example: (M1, M0, N1, N0) -> (M, N)
static Value collapseTo2D(mlir::Location loc, PatternRewriter &rewriter,
                          Value input, ArrayRef<int64_t> targetShape) {
  auto inputType = input.getType().cast<RankedTensorType>();
  auto targetType =
      RankedTensorType::get(targetShape, inputType.getElementType());
  std::array<ReassociationIndices, 2> collapseIndices = {
      ReassociationIndices{0, 1}, ReassociationIndices{2, 3}};
  Value reshapedOperand = rewriter.create<linalg::TensorCollapseShapeOp>(
      loc, targetType, input, collapseIndices);
  return reshapedOperand;
}

// Returns true if an input of the given |inputShape| needs padding to
// ensure that its shape will be a multiple of |tileShape|. That's always true
// in the dynamic shape case.
static bool needsPadding(ArrayRef<int64_t> inputShape,
                         ArrayRef<int64_t> tileShape) {
  assert(inputShape.size() == tileShape.size());
  for (int i = 0; i < inputShape.size(); i++) {
    if (inputShape[i] == ShapedType::kDynamicSize) {
      return true;
    }
    if (inputShape[i] % tileShape[i] != 0) {
      return true;
    }
  }
  return false;
}

// Pads |input| on the bottom and on the right to the next multiple of
// |tileShape|.
static Value pad(Location loc, PatternRewriter &rewriter, Value input,
                 ArrayRef<int64_t> tileShape) {
  SmallVector<OpFoldResult, 2> lowPadding, highPadding;
  SmallVector<int64_t, 2> resultTypeShape;
  RankedTensorType inputType = input.getType().cast<RankedTensorType>();
  ArrayRef<int64_t> inputShape = inputType.getShape();
  if (!needsPadding(inputShape, tileShape)) {
    return input;
  }
  int rank = inputType.getRank();
  for (int i = 0; i < rank; ++i) {
    // No 'low' padding i.e. no padding at the top and on the left.
    lowPadding.push_back(rewriter.getIndexAttr(0));
    // 'High' padding i.e. padding at the bottom and on the right, and the
    // result type shape, will be dynamic in any dimension if and only if the
    // input shape is.
    if (inputShape[i] == ShapedType::kDynamicSize) {
      resultTypeShape.push_back(ShapedType::kDynamicSize);
      // There only remains to compute the 'high' padding Value.
      auto add = [&](Value a, Value b) {
        return rewriter.create<arith::AddIOp>(loc, a, b);
      };
      auto sub = [&](Value a, Value b) {
        return rewriter.create<arith::SubIOp>(loc, a, b);
      };
      auto rem = [&](Value a, Value b) {
        return rewriter.create<arith::RemSIOp>(loc, a, b);
      };
      // Compare to the plainer distance_to_next_multiple_of in the static
      // dimension case below.
      auto distance_to_next_multiple_of = [&](Value a, Value b) {
        Value one = rewriter.create<arith::ConstantIndexOp>(loc, 1);
        Value b_minus_one = sub(b, one);
        return sub(b_minus_one, rem(add(a, b_minus_one), b));
      };
      Value inputDim = rewriter.create<tensor::DimOp>(loc, input, i);
      Value tileDim =
          rewriter.create<arith::ConstantIndexOp>(loc, tileShape[i]);
      Value padding = distance_to_next_multiple_of(inputDim, tileDim);
      highPadding.push_back(padding);
    } else {
      auto distance_to_next_multiple_of = [=](int64_t a, int64_t b) {
        int64_t b_minus_one = b - 1;
        return b_minus_one - ((a + b_minus_one) % b);
      };
      int64_t inputDim = inputShape[i];
      int64_t tileDim = tileShape[i];
      int64_t padding = distance_to_next_multiple_of(inputDim, tileDim);
      resultTypeShape.push_back(inputDim + padding);
      highPadding.push_back(rewriter.getIndexAttr(padding));
    }
  }
  Type elementType = inputType.getElementType();
  RankedTensorType resultType =
      RankedTensorType::get(resultTypeShape, elementType);
  Value padValue = rewriter.create<arith::ConstantOp>(
      loc, elementType, rewriter.getZeroAttr(elementType));
  return linalg::PadTensorOp::createPadScalarOp(
      resultType, input, padValue, lowPadding, highPadding,
      /* nofold = */ false, loc, rewriter);
}

// Returns a top-left slice from |input| shaped like |likeWhat|.
static Value extractSliceLike(Location loc, PatternRewriter &rewriter,
                              Value input, Value likeWhat) {
  SmallVector<OpFoldResult, 2> offsets, dims, strides;
  RankedTensorType resultType = likeWhat.getType().cast<RankedTensorType>();
  int rank = resultType.getRank();
  auto resultShape = likeWhat.getType().cast<ShapedType>().getShape();
  for (int i = 0; i < rank; ++i) {
    offsets.push_back(rewriter.getIndexAttr(0));
    strides.push_back(rewriter.getIndexAttr(1));
    if (resultShape[i] == ShapedType::kDynamicSize) {
      dims.emplace_back(rewriter.create<tensor::DimOp>(loc, likeWhat, i));
    } else {
      dims.push_back(rewriter.getIndexAttr(resultShape[i]));
    }
  }
  return rewriter.create<tensor::ExtractSliceOp>(loc, resultType, input,
                                                 offsets, dims, strides);
}

static bool haveEqualShapeDim(Value x, Value y, int i) {
  return x.getType().cast<ShapedType>().getDimSize(i) ==
         y.getType().cast<ShapedType>().getDimSize(i);
}

// Converts linalg.matmul to an equivalent subgraph using linalg.mmt4d.
// Currently, M0, N0, K0 are compile time constants.
// TODO(ataei): Move this pattern to linalg transforms upstream.
class LinalgMatmulOpToLinalgMmt4DOpPattern
    : public OpRewritePattern<linalg::MatmulOp> {
 public:
  LinalgMatmulOpToLinalgMmt4DOpPattern(MLIRContext *context, int M0, int K0,
                                       int N0, PatternBenefit benefit = 1)
      : OpRewritePattern<linalg::MatmulOp>(context, benefit),
        M0(M0),
        K0(K0),
        N0(N0) {}

  LogicalResult matchAndRewrite(linalg::MatmulOp matmulOp,
                                PatternRewriter &rewriter) const override {
    Location loc = matmulOp.getLoc();

    Value lhs = matmulOp.getInputOperand(0)->get();
    Value rhs = matmulOp.getInputOperand(1)->get();
    Value acc = matmulOp.getOutputOperand(0)->get();

    // This transformation supports any mixing of static and dynamic dimensions,
    // with one exception: the dynamic-ness of each dimension of the accumulator
    // must match the dynamic-ness of the corresponding lhs/rhs dimension.
    // This limitation is not inherent to this transformation's code, it's just
    // here to avoid a current linalg folding limitation: at the moment,
    // removing this gives the following error in e2e matmul tests,
    //   "error: failed to legalize operation 'tensor.cast' that was explicitly
    //   marked illegal"
    // apparently due to some missing folding of tensor.cast op into reshapes.
    if (!haveEqualShapeDim(lhs, acc, 0) || !haveEqualShapeDim(rhs, acc, 1)) {
      return failure();
    }

    Value paddedLhs = pad(loc, rewriter, lhs, {M0, K0});
    Value paddedRhs = pad(loc, rewriter, rhs, {K0, N0});
    Value paddedAcc = pad(loc, rewriter, acc, {M0, N0});

    Value lhs4D = expandTo4D(loc, rewriter, paddedLhs, {M0, K0});
    Value rhs4D = expandTo4D(loc, rewriter, paddedRhs, {K0, N0});
    Value acc4D = expandTo4D(loc, rewriter, paddedAcc, {M0, N0});

    Value lhs4DT = transpose(loc, rewriter, lhs4D, {0, 2, 1, 3});
    Value rhs4DT = transpose(loc, rewriter, rhs4D, {2, 0, 3, 1});
    Value acc4DT = transpose(loc, rewriter, acc4D, {0, 2, 1, 3});

    auto mmt4dResult = rewriter.create<linalg::Mmt4DOp>(
        loc, acc4DT.getType(), ValueRange{lhs4DT, rhs4DT}, ValueRange{acc4DT});

    Value mmt4dResultTransposed =
        transpose(loc, rewriter, mmt4dResult.getResult(0), {0, 2, 1, 3});

    Value paddedResult =
        collapseTo2D(loc, rewriter, mmt4dResultTransposed,
                     paddedAcc.getType().cast<ShapedType>().getShape());
    Value result = extractSliceLike(loc, rewriter, paddedResult, acc);

    rewriter.replaceOp(matmulOp, ArrayRef<Value>{result});

    return success();
  }

 private:
  const int M0;
  const int K0;
  const int N0;
};

/// Canonicalizes [linalg.init_tensor -> linalg.fill -> linalg.generic] ->
/// [linalg.init_tensor -> linalg.fill] where linalg.generic does only copy e.g
/// a transpose.
struct FoldFillGenericOpPattern : public OpRewritePattern<linalg::GenericOp> {
  using OpRewritePattern<linalg::GenericOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(linalg::GenericOp genericOp,
                                PatternRewriter &rewriter) const override {
    if (genericOp.getNumInputs() != 1) return failure();
    if (genericOp.getNumOutputs() != 1) return failure();

    // Check linalg.generic does have copy only semantics.
    if (genericOp.getNumParallelLoops() != genericOp.getNumLoops())
      return failure();
    auto results =
        llvm::to_vector<4>(genericOp.getBody()->getOps<linalg::YieldOp>());
    if (results.size() != 1) return failure();
    if (results[0].values().size() != 1) return failure();
    auto blockArgument = results[0].values()[0].dyn_cast<BlockArgument>();
    if (!blockArgument || blockArgument.getArgNumber() != 0) return failure();

    auto input = genericOp.inputs()[0];

    auto outputType =
        genericOp.outputs()[0].getType().dyn_cast<RankedTensorType>();

    // TODO: To enable dynamic shapes we need to apply the same permutation on
    // init tensor sizes.
    if (!outputType || !outputType.hasStaticShape()) return failure();

    auto fillOp = dyn_cast<linalg::FillOp>(input.getDefiningOp());
    if (!fillOp) return failure();

    auto loc = genericOp.getLoc();
    Value newInitTensor = rewriter.create<linalg::InitTensorOp>(
        loc, outputType.getShape(), outputType.getElementType());
    rewriter.replaceOpWithNewOp<linalg::FillOp>(genericOp, fillOp.value(),
                                                newInitTensor);

    return success();
  }
};

class ConvertLinalgMatmulToMmt4DPass final
    : public ConvertLinalgMatmulToMmt4DBase<ConvertLinalgMatmulToMmt4DPass> {
 public:
  ConvertLinalgMatmulToMmt4DPass() {}
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<linalg::LinalgDialect>();
  }

  LogicalResult initializeOptions(StringRef options) override {
    if (failed(Pass::initializeOptions(options))) return failure();
    auto failureWithMessage = [=](const char *msg) {
      llvm::errs() << "illegal options `" << options << "` for pass `"
                   << getArgument() << "`: " << msg << "\n";
      return failure();
    };
    if (M0 == mlir::ShapedType::kDynamicSize ||
        N0 == mlir::ShapedType::kDynamicSize ||
        K0 == mlir::ShapedType::kDynamicSize) {
      return failureWithMessage(
          "currently all three values M0,K0,N0 must be "
          "specified as a fixed size value, not 'dynamic', as the heuristic to "
          "choose these values is not yet implemented.");
    }
    if (M0 == 0 || N0 == 0 || K0 == 0) {
      return failureWithMessage("all three values M0,K0,N0 must be nonzero.");
    }
    return success();
  }

  void runOnOperation() override {
    MLIRContext *context = &getContext();
    // Main pattern.
    {
      OwningRewritePatternList patterns(&getContext());
      patterns.insert<LinalgMatmulOpToLinalgMmt4DOpPattern>(context, M0, K0,
                                                            N0);
      (void)applyPatternsAndFoldGreedily(getOperation(), std::move(patterns));
    }
    // Canonicalization.
    {
      OwningRewritePatternList patterns(&getContext());
      linalg::TensorExpandShapeOp::getCanonicalizationPatterns(patterns,
                                                               context);
      patterns.insert<FoldFillGenericOpPattern>(context);
      (void)applyPatternsAndFoldGreedily(getOperation(), std::move(patterns));
    }
  }
};
}  // namespace

std::unique_ptr<OperationPass<FuncOp>> createConvertLinalgMatmulToMmt4DPass() {
  return std::make_unique<ConvertLinalgMatmulToMmt4DPass>();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
