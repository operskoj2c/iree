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

//===- CovertToSPIRVPass.cpp - Pass for the final SPIR-V conversion -------===//
//
// This file implements a pass to perform the final conversion to SPIR-V.
// This pass converts remaining interface ops into SPIR-V global variables,
// GPU processor ID ops into SPIR-V global variables, loop/standard ops into
// corresponding SPIR-V ops.
//
//===----------------------------------------------------------------------===//

#include "iree/compiler/Conversion/CodegenUtils/MarkerUtils.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/CooperativeMatrixAnalysis.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/IREE/IR/IREEOps.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "mlir/Conversion/GPUToSPIRV/GPUToSPIRV.h"
#include "mlir/Conversion/SCFToSPIRV/SCFToSPIRV.h"
#include "mlir/Conversion/StandardToSPIRV/StandardToSPIRV.h"
#include "mlir/Conversion/VectorToSPIRV/VectorToSPIRV.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVTypes.h"
#include "mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Vector/VectorOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace {
//===----------------------------------------------------------------------===//
// Resource and push constant variable utilities
//===----------------------------------------------------------------------===//
// TODO(antiagainst): move these utilities to MLIR core.

/// Returns the pointer type for the push constant storage containing
/// `elementCount` 32-bit integer values.
spirv::PointerType getPushConstantStorageType(unsigned elementCount,
                                              Builder &builder) {
  auto arrayType = spirv::ArrayType::get(
      SPIRVTypeConverter::getIndexType(builder.getContext()), elementCount,
      /*stride=*/4);
  auto structType = spirv::StructType::get({arrayType}, /*offsetInfo=*/0);
  return spirv::PointerType::get(structType, spirv::StorageClass::PushConstant);
}

/// Returns the push constant varible containing `elementCount` 32-bit integer
/// values in `body`. Returns null op if such an op does not exit.
spirv::GlobalVariableOp getPushConstantVariable(Block &body,
                                                unsigned elementCount) {
  for (auto varOp : body.getOps<spirv::GlobalVariableOp>()) {
    auto ptrType = varOp.type().cast<spirv::PointerType>();
    // Note that Vulkan requires "There must be no more than one push constant
    // block statically used per shader entry point." So we should always reuse
    // the existing one.
    if (ptrType.getStorageClass() == spirv::StorageClass::PushConstant) {
      auto numElements = ptrType.getPointeeType()
                             .cast<spirv::StructType>()
                             .getElementType(0)
                             .cast<spirv::ArrayType>()
                             .getNumElements();
      if (numElements == elementCount) return varOp;
    }
  }
  return nullptr;
}

/// Gets or inserts a global variable for push constant storage containing
/// `elementCount` 32-bit integer values in `block`.
spirv::GlobalVariableOp getOrInsertPushConstantVariable(Location loc,
                                                        Block &block,
                                                        unsigned elementCount,
                                                        OpBuilder &b) {
  if (auto varOp = getPushConstantVariable(block, elementCount)) return varOp;

  auto builder = OpBuilder::atBlockBegin(&block, b.getListener());
  auto type = getPushConstantStorageType(elementCount, builder);
  StringRef name = "__push_constant_var__";
  return builder.create<spirv::GlobalVariableOp>(loc, type, name,
                                                 /*initializer=*/nullptr);
}

/// Gets the value at the given `offset` of the push constant storage. A global
/// variable will be created for the push constant storage if not existing. Load
/// ops will be created via the given `builder` to load values from the push
/// constant.
Value getPushConstantValue(Operation *op, unsigned elementCount,
                           unsigned offset, OpBuilder &builder) {
  Location loc = op->getLoc();
  Operation *parent = SymbolTable::getNearestSymbolTable(op->getParentOp());
  if (!parent) {
    op->emitError("expected operation to be within a module-like op");
    return nullptr;
  }

  spirv::GlobalVariableOp varOp = getOrInsertPushConstantVariable(
      loc, parent->getRegion(0).front(), elementCount, builder);

  auto i32Type = SPIRVTypeConverter::getIndexType(builder.getContext());
  Value zeroOp = spirv::ConstantOp::getZero(i32Type, loc, builder);
  Value offsetOp = builder.create<spirv::ConstantOp>(
      loc, i32Type, builder.getI32IntegerAttr(offset));
  auto addrOp = builder.create<spirv::AddressOfOp>(loc, varOp);
  auto acOp = builder.create<spirv::AccessChainOp>(
      loc, addrOp, llvm::makeArrayRef({zeroOp, offsetOp}));
  return builder.create<spirv::LoadOp>(loc, acOp);
}

/// Inserts a resource evariable of the given `type` into `block` and bind
/// it to `set` and `binding`. `id` uniquely identifies the inserted variable.
spirv::GlobalVariableOp insertResourceVariable(Location loc, Type type,
                                               uint64_t id, unsigned set,
                                               unsigned binding, bool alias,
                                               Block &block, OpBuilder &b) {
  auto name = llvm::formatv("__resource_var_{0}__", id).str();
  auto builder = OpBuilder::atBlockBegin(&block, b.getListener());
  auto variable =
      builder.create<spirv::GlobalVariableOp>(loc, type, name, set, binding);
  if (alias) variable->setAttr("aliased", builder.getUnitAttr());
  return variable;
}

/// Returns the IREE::HAL::InterfaceBindingOp from an interface op.
IREE::HAL::InterfaceBindingOp getBindingOp(Operation *op) {
  if (auto placeholderOp = dyn_cast<IREE::PlaceholderOp>(op)) {
    return cast<IREE::HAL::InterfaceBindingOp>(
        SymbolTable::lookupNearestSymbolFrom(
            op, op->getAttrOfType<SymbolRefAttr>("binding")));
  }
  if (auto bindingSubspanOp =
          dyn_cast<IREE::HAL::InterfaceBindingSubspanOp>(op)) {
    return bindingSubspanOp.queryBindingOp();
  }
  llvm_unreachable("unknown interface binding op");
}

/// Returns the (set, binding) pair for the given placeholder op.
std::pair<int32_t, int32_t> getPlaceholderSetAndBinding(Operation *op) {
  IREE::HAL::InterfaceBindingOp bindingOp = getBindingOp(op);
  return {bindingOp.set().getSExtValue(), bindingOp.binding().getSExtValue()};
}

/// Returns the set of resources that should be marked as aliased in SPIR-V.
llvm::DenseSet<Operation *> getAliasedResources(ModuleOp module) {
  llvm::DenseSet<Operation *> aliasedResources;

  for (FuncOp func : module.getOps<FuncOp>()) {
    // Collect all placeholder ops and their (set, binding) pairs in this
    // function.
    SmallVector<Operation *, 4> placeholderOps;
    SmallVector<std::pair<uint32_t, uint32_t>, 4> setBindings;
    llvm::DenseMap<std::pair<uint32_t, uint32_t>, unsigned> setBindingCount;
    func.walk([&](Operation *op) {
      if (isa<IREE::PlaceholderOp, IREE::HAL::InterfaceBindingSubspanOp>(op)) {
        placeholderOps.emplace_back(op);
        setBindings.emplace_back(getPlaceholderSetAndBinding(op));
        ++setBindingCount[setBindings.back()];
      }
    });

    // Perform analysis to determine whether we need to mark the resource as
    // alias. This should happen when we have multiple resources binding to the
    // same (set, binding) pair and they are used in the same function.
    for (unsigned i = 0; i < placeholderOps.size(); ++i) {
      if (setBindingCount[setBindings[i]] > 1) {
        aliasedResources.insert(placeholderOps[i]);
      }
    }
  }

  return aliasedResources;
}

}  // namespace

//===----------------------------------------------------------------------===//
// Conversion patterns and pass declarations
//===----------------------------------------------------------------------===//

namespace {
/// A pattern to convert hal.interface.load.constant into a sequence of SPIR-V
/// ops to load from a global variable representing the push constant storage.
struct HALInterfaceLoadConstantConverter final
    : public OpConversionPattern<IREE::HAL::InterfaceLoadConstantOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult matchAndRewrite(
      IREE::HAL::InterfaceLoadConstantOp loadOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override;
};

/// A pattern to convert hal.interface.workgroup.id/count into corresponding
/// SPIR-V Builtin ops.
template <typename InterfaceOpTy, spirv::BuiltIn builtin>
struct HALInterfaceWorkgroupIdAndCountConverter final
    : public OpConversionPattern<InterfaceOpTy> {
  using OpConversionPattern<InterfaceOpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      InterfaceOpTy op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    int32_t index = static_cast<int32_t>(op.dimension().getSExtValue());
    Value spirvBuiltin = spirv::getBuiltinVariableValue(op, builtin, rewriter);
    rewriter.replaceOpWithNewOp<spirv::CompositeExtractOp>(
        op, rewriter.getIntegerType(32), spirvBuiltin,
        rewriter.getI32ArrayAttr({index}));
    return success();
  }
};

/// A pattern to convert iree.placeholdder/hal.interface.binding.subspan into a
/// sequence of SPIR-V ops to get the address to a global variable representing
/// the resource buffer.
template <typename InterfaceOpTy>
struct InterfaceOpConverter final : public OpConversionPattern<InterfaceOpTy> {
  InterfaceOpConverter(TypeConverter &typeConverter, MLIRContext *context,
                       llvm::DenseSet<Operation *> &aliasedResources,
                       PatternBenefit benefit = 1)
      : OpConversionPattern<InterfaceOpTy>(typeConverter, context, benefit),
        aliasedResources(aliasedResources) {}

  LogicalResult matchAndRewrite(
      InterfaceOpTy interfaceOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    auto moduleOp = interfaceOp->template getParentOfType<ModuleOp>();

    Type resultType = interfaceOp.getOperation()->getResult(0).getType();
    Type convertedType = this->getTypeConverter()->convertType(resultType);
    if (!convertedType) {
      return interfaceOp.emitError()
             << "SPIRV type conversion failed: " << resultType;
    }
    auto bindingOp = getBindingOp(interfaceOp.getOperation());

    // We always create a new resource variable for the placeholder and use the
    // placeholder op's pointer address as the `id`.
    spirv::GlobalVariableOp varOp = insertResourceVariable(
        interfaceOp.getLoc(), convertedType,
        reinterpret_cast<uint64_t>(interfaceOp.getOperation()),
        bindingOp.set().getZExtValue(), bindingOp.binding().getZExtValue(),
        aliasedResources.contains(interfaceOp.getOperation()),
        *moduleOp.getBody(), rewriter);

    rewriter.replaceOpWithNewOp<spirv::AddressOfOp>(interfaceOp, varOp);
    return success();
  }

 private:
  const llvm::DenseSet<Operation *> &aliasedResources;
};

/// Pattern to lower operations that become a no-ops at this level.
template <typename OpTy>
struct FoldAsNoOp final : public OpConversionPattern<OpTy> {
  using OpConversionPattern<OpTy>::OpConversionPattern;
  LogicalResult matchAndRewrite(
      OpTy op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, operands);
    return success();
  }
};

/// Translates vector.transfer_read with less than 4 scalars into reading each
/// scalar and then compose the vector.
///
/// This is a very specific pattern for handling corner cases and boundary
/// cases. For example, in vision models we can have the initial image with
/// three channels. We cannot perform the native load4 there; by performing
/// scalar read we lose some benefits of load4 but we can still make sure the
/// overall vectorization does not fail.
struct ScalarizeVectorTransferRead final
    : public OpConversionPattern<vector::TransferReadOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      vector::TransferReadOp readOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override;
};

/// Base class for lowering to SPIR-V cooperative matrix ops.
template <typename SourceOp>
class CoopMatOpLowering : public OpConversionPattern<SourceOp> {
 public:
  CoopMatOpLowering(MLIRContext *context, SPIRVTypeConverter &converter,
                    // Dedicated extensions are typically faster; so give it a
                    // higher benefit so it prevails by default.
                    PatternBenefit benefit = 5)
      : OpConversionPattern<SourceOp>(context, benefit), converter(converter) {}

 protected:
  // TODO: We explicitly keep a reference of the type converter instead of
  // passing it to OpConversionPattern during construction. This effectively
  // bypasses the dialect conversion framework's automation over type
  // conversion. This is needed for now because upstream SPIRVTypeConverter does
  // not support cooperative matrix well yet so the framework won't know how to
  // generate cooperative matrix. We are manually constructing the cooperative
  // matrix in patterns. This should be fixed when we upstream all cooperative
  // matrix related code.
  SPIRVTypeConverter &converter;
};

/// Convert subgroup level vector transfert to SPIR-V cooperative
/// matrix load/store if those are supported.
/// TODO(thomasraoux): Move to MLIR core once this is stable.
template <typename OpTy>
class TransferToCoopMatLoadStore final : public CoopMatOpLowering<OpTy> {
 public:
  TransferToCoopMatLoadStore(
      MLIRContext *context, SPIRVTypeConverter &converter,
      const CooperativeMatrixAnalysis &cooperativeMatrixAnalysis)
      : CoopMatOpLowering<OpTy>(context, converter),
        cooperativeMatrixAnalysis(cooperativeMatrixAnalysis) {}

  LogicalResult matchAndRewrite(
      OpTy op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    if (!cooperativeMatrixAnalysis.usesCooperativeMatrixType(op))
      return failure();
    auto loc = op.getLoc();
    auto memrefType = op.getShapedType().template dyn_cast<MemRefType>();
    if (!memrefType) return failure();
    auto vecType = op.getVectorType();
    if (vecType.getRank() != 2) return failure();
    // TODO(thomasraoux): use coloumn major operand when TransfertRead +
    // TransposeOp.
    if (!op.permutation_map().isMinorIdentity()) return failure();
    if (op.masked() &&
        llvm::any_of(op.masked()->template cast<ArrayAttr>(),
                     [](mlir::Attribute maskedDim) {
                       return maskedDim.cast<BoolAttr>().getValue();
                     }))
      return failure();
    auto matType = spirv::CooperativeMatrixNVType::get(
        vecType.getElementType(), spirv::Scope::Subgroup, vecType.getDimSize(0),
        vecType.getDimSize(1));
    SmallVector<Value, 4> remappedIndices;
    for (auto i : op.indices())
      remappedIndices.push_back(rewriter.getRemappedValue(i));
    Value ptr = spirv::getElementPtr(
        CoopMatOpLowering<OpTy>::converter, memrefType,
        rewriter.getRemappedValue(op.source()), remappedIndices, loc, rewriter);
    int64_t offset = 0;
    SmallVector<int64_t, 2> strides;
    (void)getStridesAndOffset(memrefType, strides, offset);
    auto stride = strides[0];
    if (BaseMemRefType::isDynamicStrideOrOffset(stride)) return failure();
    auto int32Type = rewriter.getI32Type();
    auto strideValue = rewriter.create<spirv::ConstantOp>(
        loc, int32Type, IntegerAttr::get(int32Type, stride));
    auto coloumnMajor = rewriter.create<spirv::ConstantOp>(
        loc, rewriter.getI1Type(), rewriter.getBoolAttr(false));
    replaceTransferOp(op, loc, matType, ptr, strideValue, coloumnMajor,
                      rewriter);
    return success();
  }

 private:
  /// Helper to generate the right load/store instruction and replace the
  /// transfer op.
  void replaceTransferOp(OpTy op, Location loc, Type matType, Value ptr,
                         Value strideValue, Value coloumnMajor,
                         ConversionPatternRewriter &rewriter) const;
  const CooperativeMatrixAnalysis &cooperativeMatrixAnalysis;
};

template <>
void TransferToCoopMatLoadStore<vector::TransferReadOp>::replaceTransferOp(
    vector::TransferReadOp op, Location loc, Type matType, Value ptr,
    Value strideValue, Value coloumnMajor,
    ConversionPatternRewriter &rewriter) const {
  Value load = rewriter.create<spirv::CooperativeMatrixLoadNVOp>(
      loc, matType, ptr, strideValue, coloumnMajor, spirv::MemoryAccessAttr());
  rewriter.replaceOp(op, load);
}

template <>
void TransferToCoopMatLoadStore<vector::TransferWriteOp>::replaceTransferOp(
    vector::TransferWriteOp op, Location loc, Type matType, Value ptr,
    Value strideValue, Value coloumnMajor,
    ConversionPatternRewriter &rewriter) const {
  rewriter.create<spirv::CooperativeMatrixStoreNVOp>(
      loc, ptr, rewriter.getRemappedValue(op.vector()), strideValue,
      coloumnMajor, spirv::MemoryAccessAttr());
  rewriter.eraseOp(op);
}

/// Convert subgroup level vector contract to SPIR-V cooperative
/// matrix matmuladd.
class VectorContractToCoopMatmul final
    : public CoopMatOpLowering<vector::ContractionOp> {
 public:
  VectorContractToCoopMatmul(
      MLIRContext *context, SPIRVTypeConverter &converter,
      const CooperativeMatrixAnalysis &cooperativeMatrixAnalysis)
      : CoopMatOpLowering<vector::ContractionOp>(context, converter),
        cooperativeMatrixAnalysis(cooperativeMatrixAnalysis) {}

  LogicalResult matchAndRewrite(
      vector::ContractionOp contractOp, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const override {
    if (!cooperativeMatrixAnalysis.usesCooperativeMatrixType(contractOp))
      return failure();
    auto loc = contractOp.getLoc();
    // Check that all the operands are cooperative matrix.
    vector::ContractionOp::Adaptor adaptor(operands);
    auto loadA = adaptor.lhs();
    auto loadB = adaptor.rhs();
    auto loadC = adaptor.acc();
    if (!loadA.getType().isa<spirv::CooperativeMatrixNVType>() ||
        !loadB.getType().isa<spirv::CooperativeMatrixNVType>() ||
        !loadC.getType().isa<spirv::CooperativeMatrixNVType>())
      return failure();
    if (llvm::size(contractOp.masks()) != 0) return failure();
    // Check that this is a matmul operation.
    auto iteratorTypes = contractOp.iterator_types().getValue();
    if (!isParallelIterator(iteratorTypes[0]) ||
        !isParallelIterator(iteratorTypes[1]) ||
        !isReductionIterator(iteratorTypes[2]))
      return failure();
    // Coloumn major matmul should have been lowered to Transpose+contract
    // by this point. Transpose can be handled by load/stoore operations.
    if (!isRowMajorMatmul(contractOp.indexing_maps())) return failure();

    Value matmul = rewriter.create<spirv::CooperativeMatrixMulAddNVOp>(
        loc, loadC.getType(), loadA, loadB, loadC);
    rewriter.replaceOp(contractOp, matmul);
    return success();
  }

 private:
  const CooperativeMatrixAnalysis &cooperativeMatrixAnalysis;
};

/// A pass to perform the SPIR-V conversion.
///
/// This pass converts remaining interface ops into SPIR-V global variables,
/// GPU processor ID ops into SPIR-V global variables, loop/standard ops into
/// corresponding SPIR-V ops.
struct ConvertToSPIRVPass
    : public PassWrapper<ConvertToSPIRVPass, OperationPass<ModuleOp>> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<spirv::SPIRVDialect>();
  }

  void runOnOperation() override;
  ConvertToSPIRVPass() {}
  ConvertToSPIRVPass(const ConvertToSPIRVPass &pass) {}
};
}  // namespace

//===----------------------------------------------------------------------===//
// Conversion patterns and pass implementations
//===----------------------------------------------------------------------===//

LogicalResult HALInterfaceLoadConstantConverter::matchAndRewrite(
    IREE::HAL::InterfaceLoadConstantOp loadOp, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) const {
  // TODO(#1519): hal.interface.load.constant should point to the
  // hal.interface op.
  auto moduleOp = loadOp->getParentOfType<ModuleOp>();
  auto halInterfaceOps =
      llvm::to_vector<1>(moduleOp.getOps<IREE::HAL::InterfaceOp>());
  assert(halInterfaceOps.size() == 1);
  assert(halInterfaceOps.front().push_constants().hasValue());

  uint64_t elementCount =
      (*halInterfaceOps.front().push_constants()).getZExtValue();
  unsigned offset = loadOp.offset().getZExtValue();

  // The following function generates SPIR-V ops with i32 types. So it does type
  // "conversion" (index -> i32) implicitly.
  auto value = getPushConstantValue(loadOp, elementCount, offset, rewriter);

  rewriter.replaceOp(loadOp, value);
  return success();
}

LogicalResult ScalarizeVectorTransferRead::matchAndRewrite(
    vector::TransferReadOp readOp, ArrayRef<Value> operands,
    ConversionPatternRewriter &rewriter) const {
  VectorType vectorType = readOp.getType();
  Type scalarType = vectorType.getElementType();
  if (vectorType.getRank() != 1 || vectorType.getDimSize(0) >= 4)
    return failure();

  Location loc = readOp.getLoc();
  vector::TransferReadOp::Adaptor adaptor(operands);

  SmallVector<Value, 4> scalars;
  SmallVector<Value, 4> indices(adaptor.indices().begin(),
                                adaptor.indices().end());
  for (int i = 0; i < vectorType.getDimSize(0); ++i) {
    indices.back() = rewriter.createOrFold<ConstantIndexOp>(loc, i);
    scalars.push_back(rewriter.create<memref::LoadOp>(
        loc, scalarType, readOp.source(), indices));
  }

  rewriter.replaceOpWithNewOp<spirv::CompositeConstructOp>(readOp, vectorType,
                                                           scalars);
  return success();
}

static void populateVectorToSPIRVPatterns(
    MLIRContext *context, SPIRVTypeConverter &converter,
    OwningRewritePatternList &patterns,
    const CooperativeMatrixAnalysis &cooperativeMatrixAnalysis) {
  patterns.insert<TransferToCoopMatLoadStore<vector::TransferReadOp>,
                  TransferToCoopMatLoadStore<vector::TransferWriteOp>,
                  VectorContractToCoopMatmul>(context, converter,
                                              cooperativeMatrixAnalysis);
}

void ConvertToSPIRVPass::runOnOperation() {
  MLIRContext *context = &getContext();
  ModuleOp moduleOp = getOperation();

  auto targetAttr = spirv::lookupTargetEnv(moduleOp);
  SPIRVTypeConverter typeConverter(targetAttr);
  ScfToSPIRVContext scfToSPIRVContext;

  OwningRewritePatternList patterns(&getContext());
  // Pull in GPU patterns to convert processor ID ops and loop ops.
  populateGPUToSPIRVPatterns(typeConverter, patterns);
  // Pull in SCF patterns to convert control flow ops.
  populateSCFToSPIRVPatterns(typeConverter, scfToSPIRVContext, patterns);
  // Pull in standard patterns to convert arithmetic ops and others.
  populateStandardToSPIRVPatterns(typeConverter, patterns);
  // Pull in standard patterns to convert tensor operations to SPIR-V. These are
  // primarily used to handle tensor-type constants and contain a
  // threshold. Only those constants that are below the threshold are converted
  // to SPIR-V. In IREE we want to control this threshold at Flow level. So set
  // this value arbitrarily high to make sure that everything within a dispatch
  // region is converted.
  mlir::populateTensorToSPIRVPatterns(
      typeConverter, std::numeric_limits<int64_t>::max() / 8, patterns);
  // Pull in vector patterns to convert vector ops.
  mlir::populateVectorToSPIRVPatterns(typeConverter, patterns);
  // Pull in builtin func to spv.func conversion.
  populateBuiltinFuncToSPIRVPatterns(typeConverter, patterns);
  auto &cooperativeMatrixAnalysis = getAnalysis<CooperativeMatrixAnalysis>();
  populateVectorToSPIRVPatterns(context, typeConverter, patterns,
                                cooperativeMatrixAnalysis);
  patterns.insert<
      HALInterfaceLoadConstantConverter,
      HALInterfaceWorkgroupIdAndCountConverter<
          IREE::HAL::InterfaceWorkgroupIDOp, spirv::BuiltIn::WorkgroupId>,
      HALInterfaceWorkgroupIdAndCountConverter<
          IREE::HAL::InterfaceWorkgroupCountOp, spirv::BuiltIn::NumWorkgroups>,
      ScalarizeVectorTransferRead>(typeConverter, context);
  auto aliasedResources = getAliasedResources(moduleOp);
  patterns.insert<InterfaceOpConverter<IREE::PlaceholderOp>,
                  InterfaceOpConverter<IREE::HAL::InterfaceBindingSubspanOp>>(
      typeConverter, context, aliasedResources);
  /// Fold operations as no-ops
  /// - linalg.reshape becomes a no-op since all memrefs are linearized in
  ///   SPIR-V
  /// - tensor_to_memref can become a no-op since tensors are lowered to
  ///   !spv.array
  patterns
      .insert<FoldAsNoOp<linalg::ReshapeOp>, FoldAsNoOp<memref::BufferCastOp>>(
          typeConverter, context);

  std::unique_ptr<ConversionTarget> target =
      spirv::SPIRVConversionTarget::get(targetAttr);
  // Disallow all other ops.
  target->markUnknownOpDynamicallyLegal([](Operation *) { return false; });
  SmallVector<FuncOp, 1> functions;
  for (FuncOp fn : moduleOp.getOps<FuncOp>()) {
    if (!fn.isPublic()) continue;
    functions.push_back(fn);
  }

  FrozenRewritePatternSet frozenPatterns(std::move(patterns));
  for (FuncOp fn : functions)
    if (failed(applyFullConversion(fn, *target, frozenPatterns)))
      return signalPassFailure();

  // Collect all SPIR-V ops into a spv.module.
  auto builder = OpBuilder::atBlockBegin(moduleOp.getBody());
  auto spvModule = builder.create<spirv::ModuleOp>(
      moduleOp.getLoc(), spirv::AddressingModel::Logical,
      spirv::MemoryModel::GLSL450);
  Operation *terminator = spvModule.getBlock().getTerminator();
  Dialect *spvDialect = spvModule->getDialect();
  for (Operation &op : llvm::make_early_inc_range(*moduleOp.getBody())) {
    // Skip the newly created spv.module itself.
    if (&op == spvModule) continue;
    if (op.getDialect() == spvDialect) op.moveBefore(terminator);
  }
}

//===----------------------------------------------------------------------===//
// Pass entry point and registration
//===----------------------------------------------------------------------===//

std::unique_ptr<OperationPass<ModuleOp>> createConvertToSPIRVPass() {
  return std::make_unique<ConvertToSPIRVPass>();
}

static PassRegistration<ConvertToSPIRVPass> pass(
    "iree-codegen-convert-to-spirv",
    "Perform final conversion from builtin/GPU/HAL/standard dialect to SPIR-V "
    "dialect");
}  // namespace iree_compiler
}  // namespace mlir
