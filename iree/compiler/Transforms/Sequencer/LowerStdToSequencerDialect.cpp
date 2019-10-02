// Copyright 2019 Google LLC
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

#include "iree/compiler/IR/Dialect.h"
#include "iree/compiler/IR/Ops.h"
#include "iree/compiler/IR/Sequencer/HLDialect.h"
#include "iree/compiler/IR/Sequencer/HLOps.h"
#include "iree/compiler/IR/StructureOps.h"
#include "iree/compiler/Utils/MemRefUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/StandardOps/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Utils.h"

namespace mlir {
namespace iree_compiler {

namespace {

class SequencerConversionPattern : public ConversionPattern {
 public:
  SequencerConversionPattern(StringRef operationName, int benefit,
                             MLIRContext *context,
                             MemRefTypeConverter &typeConverter)
      : ConversionPattern(operationName, benefit, context),
        typeConverter_(typeConverter) {}

 protected:
  MemRefTypeConverter &typeConverter_;
};

struct ConstantOpLowering : public SequencerConversionPattern {
  ConstantOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(ConstantOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    const auto &valueAttr = cast<ConstantOp>(op).getValue();
    auto midOp = rewriter.create<IREE::ConstantOp>(op->getLoc(), valueAttr);

    auto result = wrapAsTensor(midOp.getResult(), op, rewriter);
    rewriter.replaceOp(
        op, {loadResultValue(op->getLoc(), op->getResult(0)->getType(), result,
                             rewriter)});
    return matchSuccess();
  }
};

class CallOpLowering : public SequencerConversionPattern {
 public:
  CallOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(CallOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto callOp = cast<CallOp>(op);

    SmallVector<Type, 4> convertedResults;
    auto result = typeConverter_.convertTypes(
        callOp.getCalleeType().getResults(), convertedResults);
    (void)result;
    assert(succeeded(result) && "expected valid callee type conversion");
    rewriter.replaceOpWithNewOp<IREESeq::HL::CallOp>(
        op, callOp.getCallee(), convertedResults, operands);

    return matchSuccess();
  }
};

class CallIndirectOpLowering : public SequencerConversionPattern {
 public:
  CallIndirectOpLowering(MLIRContext *context,
                         MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(CallIndirectOp::getOperationName(), 1,
                                   context, typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto callOp = cast<CallIndirectOp>(op);
    rewriter.replaceOpWithNewOp<IREESeq::HL::CallIndirectOp>(
        op, callOp.getCallee(), operands);
    return matchSuccess();
  }
};

struct ReturnOpLowering : public SequencerConversionPattern {
  ReturnOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(ReturnOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    SmallVector<Value *, 4> newOperands;
    newOperands.reserve(operands.size());
    for (auto *operand : operands) {
      newOperands.push_back(wrapAsMemRef(operand, op, rewriter));
    }
    rewriter.replaceOpWithNewOp<IREESeq::HL::ReturnOp>(op, newOperands);
    return matchSuccess();
  }
};

struct BranchOpLowering : public SequencerConversionPattern {
  BranchOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(BranchOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> properOperands,
      ArrayRef<Block *> destinations, ArrayRef<ArrayRef<Value *>> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<IREESeq::HL::BranchOp>(op, destinations[0],
                                                       operands[0]);
    return this->matchSuccess();
  }
};

struct CondBranchOpLowering : public SequencerConversionPattern {
  CondBranchOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(CondBranchOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> properOperands,
      ArrayRef<Block *> destinations, ArrayRef<ArrayRef<Value *>> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto *condValue =
        loadAccessValue(op->getLoc(), properOperands[0], rewriter);
    rewriter.replaceOpWithNewOp<IREESeq::HL::CondBranchOp>(
        op, condValue, destinations[IREESeq::HL::CondBranchOp::trueIndex],
        operands[IREESeq::HL::CondBranchOp::trueIndex],
        destinations[IREESeq::HL::CondBranchOp::falseIndex],
        operands[IREESeq::HL::CondBranchOp::falseIndex]);
    return this->matchSuccess();
  }
};

class AllocOpLowering : public SequencerConversionPattern {
 public:
  AllocOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(AllocOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    // TODO(benvanik): replace with length computation.
    rewriter.replaceOpWithNewOp<IREESeq::HL::AllocHeapOp>(
        op, *op->getResultTypes().begin(), operands);
    return matchSuccess();
  }
};

class DeallocOpLowering : public SequencerConversionPattern {
 public:
  DeallocOpLowering(MLIRContext *context, MemRefTypeConverter &typeConverter)
      : SequencerConversionPattern(DeallocOp::getOperationName(), 1, context,
                                   typeConverter) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value *> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<IREESeq::HL::DiscardOp>(op, operands[0]);
    return matchSuccess();
  }
};

}  // namespace

void populateLowerStdToSequencerPatterns(OwningRewritePatternList &patterns,
                                         MemRefTypeConverter &converter,
                                         MLIRContext *context) {
  patterns.insert<ConstantOpLowering,
                  // Control flow.
                  CallOpLowering, CallIndirectOpLowering, ReturnOpLowering,
                  BranchOpLowering, CondBranchOpLowering,
                  // Memory management.
                  AllocOpLowering, DeallocOpLowering>(context, converter);
}

}  // namespace iree_compiler
}  // namespace mlir
