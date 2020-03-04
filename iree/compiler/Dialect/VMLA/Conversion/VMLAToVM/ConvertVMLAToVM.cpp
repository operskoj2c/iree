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

#include "iree/compiler/Dialect/VMLA/Conversion/VMLAToVM/ConvertVMLAToVM.h"

#include "iree/compiler/Dialect/IREE/IR/IREETypes.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "iree/compiler/Dialect/VM/Conversion/ConversionTarget.h"
#include "iree/compiler/Dialect/VM/Conversion/ImportUtils.h"
#include "iree/compiler/Dialect/VM/Conversion/StandardToVM/ConvertStandardToVM.h"
#include "iree/compiler/Dialect/VM/Conversion/TypeConverter.h"
#include "iree/compiler/Dialect/VM/IR/VMOps.h"
#include "iree/compiler/Dialect/VMLA/IR/VMLAOps.h"
#include "iree/compiler/Dialect/VMLA/IR/VMLATypes.h"
#include "iree/compiler/Dialect/VMLA/vmla.imports.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace {

// Erases an op. This should only be used for ops that are legalized away
// as part of lowering (i.e. tagging or metadata ops that are unrepresentable
// in the VM dialect).
class EraseNonVMOp : public ConversionPattern {
 public:
  EraseNonVMOp(StringRef rootName, MLIRContext *ctx)
      : ConversionPattern(rootName, 0, ctx) {}

  PatternMatchResult matchAndRewrite(
      Operation *op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.eraseOp(op);
    return matchSuccess();
  }
};

// VMLA -> VM import conversion base for generic ops.
// Handles signatures with integers, VM types, or simple buffers.
template <typename T, typename Adaptor = typename T::OperandAdaptor>
class VMLAImportOpConversion : public OpConversionPattern<T> {
 public:
  VMLAImportOpConversion(MLIRContext *context, SymbolTable &importSymbols,
                         TypeConverter &typeConverter, StringRef importName)
      : OpConversionPattern<T>(context),
        importSymbols(importSymbols),
        typeConverter(typeConverter),
        importName(importName) {}

  PatternMatchResult matchAndRewrite(
      T op, llvm::ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto importOp = importSymbols.template lookup<IREE::VM::ImportOp>(
        importName + getImportSuffix(op));
    assert(importOp);
    return succeeded(rewriteToCall(op, Adaptor{operands}, importOp,
                                   typeConverter, rewriter))
               ? this->matchSuccess()
               : this->matchFailure();
  }

 protected:
  virtual std::string getImportSuffix(T op) const { return ""; }

  std::string getSizedTypeStr(Type elementType) const {
    return "x" + std::to_string(elementType.getIntOrFloatBitWidth());
  }

  std::string getTypedTypeStr(Type type, bool forceUnsigned = false) const {
    Type elementType = type;
    auto shapedType = type.dyn_cast<ShapedType>();
    if (shapedType) {
      elementType = shapedType.getElementType();
    }

    std::string typePrefix = "x";
    if (elementType.isa<FloatType>()) {
      typePrefix = "f";
    } else if (elementType.isSignlessInteger()) {
      typePrefix = forceUnsigned ? "u" : "i";
    }
    return typePrefix + std::to_string(elementType.getIntOrFloatBitWidth());
  }

 private:
  SymbolTable &importSymbols;
  TypeConverter &typeConverter;
  std::string importName;
};
#define VMLA_IMPORT_OP(op_type, op_mnemonic)        \
  patterns.insert<VMLAImportOpConversion<op_type>>( \
      context, importSymbols, typeConverter, op_mnemonic);

// VMLA -> VM import conversion for ops using sized operands (foo.xNN).
// This will use only the bit-width of the element type to add a .xNN suffix to
// the op name. Assumes the element type is valid.
template <typename T>
class VMLASizedImportOpConversion : public VMLAImportOpConversion<T> {
 public:
  using VMLAImportOpConversion<T>::VMLAImportOpConversion;

  std::string getImportSuffix(T op) const override {
    return std::string(".") + this->getSizedTypeStr(op.element_type());
  }
};
#define VMLA_SIZED_IMPORT_OP(op_type, op_mnemonic)       \
  patterns.insert<VMLASizedImportOpConversion<op_type>>( \
      context, importSymbols, typeConverter, op_mnemonic);

// VMLA -> VM import conversion for ops using typed operands (foo.fNN, etc).
// This will use the element type to add a type-specific suffix to the op name.
// Assumes the element type is valid.
template <typename T>
class VMLATypedImportOpConversion : public VMLAImportOpConversion<T> {
 public:
  using VMLAImportOpConversion<T>::VMLAImportOpConversion;

  std::string getImportSuffix(T op) const override {
    bool forceUnsigned =
        !!static_cast<Operation *>(op)->getAttr("forceUnsigned");
    return "." + this->getTypedTypeStr(op.element_type(), forceUnsigned);
  }
};
#define VMLA_TYPED_IMPORT_OP(op_type, op_mnemonic)       \
  patterns.insert<VMLATypedImportOpConversion<op_type>>( \
      context, importSymbols, typeConverter, op_mnemonic);

class VMLAConstantOpConversion
    : public OpConversionPattern<IREE::VMLA::ConstantOp> {
 public:
  VMLAConstantOpConversion(MLIRContext *context, TypeConverter &typeConverter)
      : OpConversionPattern(context), typeConverter(typeConverter) {}

  PatternMatchResult matchAndRewrite(
      IREE::VMLA::ConstantOp op, llvm::ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    // Encode constant data into a rodata segment. These will eventually get
    // deduped and combined.
    auto ip = rewriter.saveInsertionPoint();
    auto parentFuncOp = op.getParentOfType<IREE::VM::FuncOp>();
    rewriter.setInsertionPoint(parentFuncOp);
    auto constName = (parentFuncOp.getName() + "_const_" +
                      std::to_string(allocateUniqueId(parentFuncOp)))
                         .str();
    auto rodataOp =
        rewriter.create<IREE::VM::RodataOp>(op.getLoc(), constName, op.value());
    rewriter.restoreInsertionPoint(ip);
    auto loadRodataOp =
        rewriter.create<IREE::VM::ConstRefRodataOp>(op.getLoc(), rodataOp);

    // Dereference constant data.
    rewriter.replaceOpWithNewOp<IREE::VMLA::BufferConstOp>(
        op, IREE::VMLA::BufferType::get(op.getContext()),
        loadRodataOp.getResult());
    return matchSuccess();
  }

 private:
  TypeConverter &typeConverter;

  // TODO(b/145839814): find a name that's unique or make the rewriter support
  // assigning unique names.
  int allocateUniqueId(Operation *context) const {
    if (uniqueContext != context) {
      uniqueContext = context;
      uniqueCounter = 0;
    }
    return uniqueCounter++;
  }
  mutable Operation *uniqueContext = nullptr;
  mutable int uniqueCounter = 0;
};

class VMLAConvertImportOpConversion
    : public VMLAImportOpConversion<IREE::VMLA::ConvertOp> {
 public:
  using VMLAImportOpConversion<IREE::VMLA::ConvertOp>::VMLAImportOpConversion;

  std::string getImportSuffix(IREE::VMLA::ConvertOp op) const override {
    return std::string(".") + getTypedTypeStr(op.src_type()) +
           std::string(".") + getTypedTypeStr(op.dst_type());
  }
};

class VMLAMatMulImportOpConversion
    : public VMLAImportOpConversion<IREE::VMLA::MatMulOp> {
 public:
  using VMLAImportOpConversion<IREE::VMLA::MatMulOp>::VMLAImportOpConversion;

  std::string getImportSuffix(IREE::VMLA::MatMulOp op) const override {
    return std::string(".") + getTypedTypeStr(op.lhs_type()) +
           getTypedTypeStr(op.rhs_type()) + std::string(".") +
           getTypedTypeStr(op.dst_type());
  }
};
}  // namespace

void populateVMLAToVMPatterns(MLIRContext *context, SymbolTable &importSymbols,
                              OwningRewritePatternList &patterns,
                              TypeConverter &typeConverter) {
  patterns.insert<VMLAConstantOpConversion>(context, typeConverter);
  patterns.insert<EraseNonVMOp>(Shape::ConstRankedShapeOp::getOperationName(),
                                context);

  VMLA_IMPORT_OP(IREE::VMLA::BufferConstOp, "vmla.buffer.const");
  VMLA_IMPORT_OP(IREE::VMLA::BufferAllocOp, "vmla.buffer.alloc");
  VMLA_IMPORT_OP(IREE::VMLA::BufferCloneOp, "vmla.buffer.clone");
  VMLA_IMPORT_OP(IREE::VMLA::BufferByteLengthOp, "vmla.buffer.byte_length");
  VMLA_IMPORT_OP(IREE::VMLA::BufferViewOp, "vmla.buffer.view");
  VMLA_IMPORT_OP(IREE::VMLA::BufferCopyOp, "vmla.buffer.copy");
  VMLA_IMPORT_OP(IREE::VMLA::BufferFillOp, "vmla.buffer.fill");
  VMLA_IMPORT_OP(IREE::VMLA::BufferLoadI32Op, "vmla.buffer.load.i32");

  VMLA_TYPED_IMPORT_OP(IREE::VMLA::CmpOp, "vmla.cmp");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::SelectOp, "vmla.select");

  VMLA_SIZED_IMPORT_OP(IREE::VMLA::CopyOp, "vmla.copy");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::TransposeOp, "vmla.transpose");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::ReverseOp, "vmla.reverse");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::PadOp, "vmla.pad");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::BroadcastOp, "vmla.broadcast");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::TileOp, "vmla.tile");

  VMLA_SIZED_IMPORT_OP(IREE::VMLA::NotOp, "vmla.not");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::AndOp, "vmla.and");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::OrOp, "vmla.or");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::XorOp, "vmla.xor");
  VMLA_SIZED_IMPORT_OP(IREE::VMLA::ShlOp, "vmla.shl");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::ShrOp, "vmla.shr");

  VMLA_TYPED_IMPORT_OP(IREE::VMLA::AddOp, "vmla.add");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::SubOp, "vmla.sub");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::AbsOp, "vmla.abs");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::NegOp, "vmla.neg");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::MulOp, "vmla.mul");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::DivOp, "vmla.div");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::RemOp, "vmla.rem");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::PowOp, "vmla.pow");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::ExpOp, "vmla.exp");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::LogOp, "vmla.log");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::RsqrtOp, "vmla.rsqrt");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::SqrtOp, "vmla.sqrt");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::CosOp, "vmla.cos");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::SinOp, "vmla.sin");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::TanhOp, "vmla.tanh");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::Atan2Op, "vmla.atan2");

  VMLA_TYPED_IMPORT_OP(IREE::VMLA::MinOp, "vmla.min");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::MaxOp, "vmla.max");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::FloorOp, "vmla.floor");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::CeilOp, "vmla.ceil");

  patterns.insert<VMLAConvertImportOpConversion>(context, importSymbols,
                                                 typeConverter, "vmla.convert");
  patterns.insert<VMLAMatMulImportOpConversion>(context, importSymbols,
                                                typeConverter, "vmla.matmul");

  VMLA_TYPED_IMPORT_OP(IREE::VMLA::ReduceSumOp, "vmla.reduce.sum");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::ReduceMinOp, "vmla.reduce.min");
  VMLA_TYPED_IMPORT_OP(IREE::VMLA::ReduceMaxOp, "vmla.reduce.max");

  VMLA_IMPORT_OP(IREE::VMLA::InterfaceBindingOp, "vmla.interface.binding");
}

namespace {

// A pass converting the IREE flow dialect into the IREE VMLA dialect.
class ConvertVMLAToVMPass : public ModulePass<ConvertVMLAToVMPass> {
 public:
  void runOnModule() override {
    auto *context = &getContext();

    VMConversionTarget conversionTarget(context);
    VMTypeConverter typeConverter;

    mlir::ModuleOp outerModuleOp, innerModuleOp;
    std::tie(outerModuleOp, innerModuleOp) =
        VMConversionTarget::nestModuleForConversion(getModule());

    appendImportModule(
        StringRef(vmla_imports_create()->data, vmla_imports_create()->size),
        innerModuleOp);

    OwningRewritePatternList conversionPatterns;
    populateStandardToVMPatterns(context, conversionPatterns);

    SymbolTable importSymbols(innerModuleOp);
    populateVMLAToVMPatterns(context, importSymbols, conversionPatterns,
                             typeConverter);

    if (failed(applyPartialConversion(outerModuleOp, conversionTarget,
                                      conversionPatterns, &typeConverter))) {
      outerModuleOp.emitError() << "conversion to vm.module failed";
      return signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OpPassBase<ModuleOp>> createConvertVMLAToVMPass() {
  return std::make_unique<ConvertVMLAToVMPass>();  // NOLINT
}

static PassRegistration<ConvertVMLAToVMPass> pass(
    "iree-convert-vmla-to-vm",
    "Convert the IREE VMLA dialect to the IREE VM dialect");

}  // namespace iree_compiler
}  // namespace mlir
