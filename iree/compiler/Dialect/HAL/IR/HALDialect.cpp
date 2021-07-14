// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/IR/HALDialect.h"

#include "iree/compiler/Dialect/HAL/Conversion/HALToHAL/ConvertHALToHAL.h"
#include "iree/compiler/Dialect/HAL/Conversion/HALToVM/ConvertHALToVM.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/IR/HALTypes.h"
#include "iree/compiler/Dialect/HAL/IR/LoweringConfig.h"
#include "iree/compiler/Dialect/HAL/hal.imports.h"
#include "iree/compiler/Dialect/IREE/IR/IREEDialect.h"
#include "iree/compiler/Dialect/VM/Conversion/ConversionDialectInterface.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/SourceMgr.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Parser.h"
#include "mlir/Transforms/InliningUtils.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

namespace {

// Used to control inlining behavior.
struct HALInlinerInterface : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  bool isLegalToInline(Operation *call, Operation *callable,
                       bool wouldBeCloned) const final {
    // Sure!
    return true;
  }

  bool isLegalToInline(Region *dest, Region *src, bool wouldBeCloned,
                       BlockAndValueMapping &valueMapping) const final {
    // Sure!
    return true;
  }

  bool isLegalToInline(Operation *op, Region *dest, bool wouldBeCloned,
                       BlockAndValueMapping &valueMapping) const final {
    // Sure!
    return true;
  }
};

class HALToVMConversionInterface : public VMConversionDialectInterface {
 public:
  using VMConversionDialectInterface::VMConversionDialectInterface;

  OwningModuleRef parseVMImportModule() const override {
    return mlir::parseSourceString(StringRef(iree_hal_imports_create()->data,
                                             iree_hal_imports_create()->size),
                                   getDialect()->getContext());
  }

  void populateVMConversionPatterns(
      SymbolTable &importSymbols, OwningRewritePatternList &patterns,
      TypeConverter &typeConverter) const override {
    populateHALToHALPatterns(getDialect()->getContext(), patterns,
                             typeConverter);
    populateHALToVMPatterns(getDialect()->getContext(), importSymbols, patterns,
                            typeConverter);
  }

  void walkAttributeStorage(
      Attribute attr,
      const function_ref<void(Attribute elementAttr)> &fn) const override {
    if (auto structAttr = attr.dyn_cast<DescriptorSetLayoutBindingAttr>()) {
      structAttr.walkStorage(fn);
    }
  }
};

class LoweringConfigAsmDialectInterface : public OpAsmDialectInterface {
  using OpAsmDialectInterface::OpAsmDialectInterface;

  LogicalResult getAlias(Attribute attr, raw_ostream &os) const override {
    if (attr.isa<LoweringConfig>()) {
      os << "config";
      return success();
    }
    return failure();
  }
};

}  // namespace

HALDialect::HALDialect(MLIRContext *context)
    : Dialect(getDialectNamespace(), context, TypeID::get<HALDialect>()) {
  context->loadDialect<IREEDialect>();

  registerAttributes();
  registerTypes();

#define GET_OP_LIST
  addOperations<
#include "iree/compiler/Dialect/HAL/IR/HALOps.cpp.inc"
      >();
  addInterfaces<HALInlinerInterface, HALToVMConversionInterface,
                LoweringConfigAsmDialectInterface>();
}

//===----------------------------------------------------------------------===//
// Attribute printing and parsing
//===----------------------------------------------------------------------===//

Attribute HALDialect::parseAttribute(DialectAsmParser &parser,
                                     Type type) const {
  StringRef attrKind;
  if (failed(parser.parseKeyword(&attrKind))) return {};
  if (attrKind == BufferConstraintsAttr::getKindName()) {
    return BufferConstraintsAttr::parse(parser);
  } else if (attrKind == ByteRangeAttr::getKindName()) {
    return ByteRangeAttr::parse(parser);
  } else if (attrKind == DescriptorSetLayoutBindingAttr::getKindName()) {
    return DescriptorSetLayoutBindingAttr::parse(parser);
  } else if (attrKind == MatchAlwaysAttr::getKindName()) {
    return MatchAlwaysAttr::parse(parser);
  } else if (attrKind == MatchAnyAttr::getKindName()) {
    return MatchAnyAttr::parse(parser);
  } else if (attrKind == MatchAllAttr::getKindName()) {
    return MatchAllAttr::parse(parser);
  } else if (attrKind == DeviceMatchIDAttr::getKindName()) {
    return DeviceMatchIDAttr::parse(parser);
  } else if (attrKind == DeviceMatchMemoryModelAttr::getKindName()) {
    return DeviceMatchMemoryModelAttr::parse(parser);
  } else if (attrKind == DeviceMatchFeatureAttr::getKindName()) {
    return DeviceMatchFeatureAttr::parse(parser);
  } else if (attrKind == DeviceMatchArchitectureAttr::getKindName()) {
    return DeviceMatchArchitectureAttr::parse(parser);
  } else if (attrKind == ExConstantStorageAttr::getKindName()) {
    return ExConstantStorageAttr::parse(parser);
  } else if (attrKind == ExPushConstantAttr::getKindName()) {
    return ExPushConstantAttr::parse(parser);
  } else if (attrKind == ExOperandBufferAttr::getKindName()) {
    return ExOperandBufferAttr::parse(parser);
  } else if (attrKind == ExResultBufferAttr::getKindName()) {
    return ExResultBufferAttr::parse(parser);
  }
  parser.emitError(parser.getNameLoc())
      << "unknown HAL attribute: " << attrKind;
  return {};
}

void HALDialect::printAttribute(Attribute attr, DialectAsmPrinter &p) const {
  TypeSwitch<Attribute>(attr)
      .Case<BufferConstraintsAttr, ByteRangeAttr,
            DescriptorSetLayoutBindingAttr,
            //
            ExConstantStorageAttr, ExPushConstantAttr, ExOperandBufferAttr,
            ExResultBufferAttr,
            //
            MatchAlwaysAttr, MatchAnyAttr, MatchAllAttr, DeviceMatchIDAttr,
            DeviceMatchMemoryModelAttr>(
          [&](auto typedAttr) { typedAttr.print(p); })
      .Default(
          [](Attribute) { llvm_unreachable("unhandled HAL attribute kind"); });
}

//===----------------------------------------------------------------------===//
// Type printing and parsing
//===----------------------------------------------------------------------===//

Type HALDialect::parseType(DialectAsmParser &parser) const {
  StringRef typeKind;
  if (parser.parseKeyword(&typeKind)) return {};
  auto type =
      llvm::StringSwitch<Type>(typeKind)
          .Case("allocator", AllocatorType::get(getContext()))
          .Case("buffer", BufferType::get(getContext()))
          .Case("buffer_view", BufferViewType::get(getContext()))
          .Case("command_buffer", CommandBufferType::get(getContext()))
          .Case("descriptor_set", DescriptorSetType::get(getContext()))
          .Case("descriptor_set_layout",
                DescriptorSetLayoutType::get(getContext()))
          .Case("device", DeviceType::get(getContext()))
          .Case("event", EventType::get(getContext()))
          .Case("executable", ExecutableType::get(getContext()))
          .Case("executable_layout", ExecutableLayoutType::get(getContext()))
          .Case("ring_buffer", RingBufferType::get(getContext()))
          .Case("semaphore", SemaphoreType::get(getContext()))
          .Default(nullptr);
  if (!type) {
    parser.emitError(parser.getCurrentLocation())
        << "unknown HAL type: " << typeKind;
  }
  return type;
}

void HALDialect::printType(Type type, DialectAsmPrinter &p) const {
  if (type.isa<AllocatorType>()) {
    p << "allocator";
  } else if (type.isa<BufferType>()) {
    p << "buffer";
  } else if (type.isa<BufferViewType>()) {
    p << "buffer_view";
  } else if (type.isa<CommandBufferType>()) {
    p << "command_buffer";
  } else if (type.isa<DescriptorSetType>()) {
    p << "descriptor_set";
  } else if (type.isa<DescriptorSetLayoutType>()) {
    p << "descriptor_set_layout";
  } else if (type.isa<DeviceType>()) {
    p << "device";
  } else if (type.isa<EventType>()) {
    p << "event";
  } else if (type.isa<ExecutableType>()) {
    p << "executable";
  } else if (type.isa<ExecutableLayoutType>()) {
    p << "executable_layout";
  } else if (type.isa<RingBufferType>()) {
    p << "ring_buffer";
  } else if (type.isa<SemaphoreType>()) {
    p << "semaphore";
  } else {
    llvm_unreachable("unknown HAL type");
  }
}

//===----------------------------------------------------------------------===//
// Dialect hooks
//===----------------------------------------------------------------------===//

Operation *HALDialect::materializeConstant(OpBuilder &builder, Attribute value,
                                           Type type, Location loc) {
  if (type.isa<IndexType>()) {
    // Some folders materialize raw index types, which just become std
    // constants.
    return builder.create<mlir::ConstantIndexOp>(
        loc, value.cast<IntegerAttr>().getValue().getSExtValue());
  }
  return nullptr;
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
