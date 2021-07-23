// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/VM/IR/VMOps.h"

#include "iree/compiler/Dialect/IREE/IR/IREETypes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/FunctionImplementation.h"
#include "mlir/IR/FunctionSupport.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace VM {

//===----------------------------------------------------------------------===//
// custom<SymbolVisibility>($sym_visibility)
//===----------------------------------------------------------------------===//
// some.op custom<SymbolVisibility>($sym_visibility) $sym_name
// ->
// some.op @foo
// some.op private @foo

static ParseResult parseSymbolVisibility(OpAsmParser &parser,
                                         StringAttr &symVisibilityAttr) {
  StringRef symVisibility;
  parser.parseOptionalKeyword(&symVisibility, {"public", "private", "nested"});
  if (!symVisibility.empty()) {
    symVisibilityAttr = parser.getBuilder().getStringAttr(symVisibility);
  }
  return success();
}

static void printSymbolVisibility(OpAsmPrinter &p, Operation *op,
                                  StringAttr symVisibilityAttr) {
  if (!symVisibilityAttr) return;
  p << symVisibilityAttr.getValue();
}

//===----------------------------------------------------------------------===//
// custom<TypeOrAttr>($type, $attr)
//===----------------------------------------------------------------------===//
// some.op custom<TypeOrAttr>($type, $attr)
// ->
// some.op : i32
// some.op = 42 : i32

static ParseResult parseTypeOrAttr(OpAsmParser &parser, TypeAttr &typeAttr,
                                   Attribute &attr) {
  if (succeeded(parser.parseOptionalEqual())) {
    if (failed(parser.parseAttribute(attr))) {
      return parser.emitError(parser.getCurrentLocation())
             << "expected attribute";
    }
    typeAttr = TypeAttr::get(attr.getType());
  } else {
    Type type;
    if (failed(parser.parseColonType(type))) {
      return parser.emitError(parser.getCurrentLocation()) << "expected type";
    }
    typeAttr = TypeAttr::get(type);
  }
  return success();
}

static void printTypeOrAttr(OpAsmPrinter &p, Operation *op, TypeAttr type,
                            Attribute attr) {
  if (attr) {
    p << " = ";
    p.printAttribute(attr);
  } else {
    p << " : ";
    p.printAttribute(type);
  }
}

//===----------------------------------------------------------------------===//
// Structural ops
//===----------------------------------------------------------------------===//

void ModuleOp::build(OpBuilder &builder, OperationState &result,
                     StringRef name) {
  ensureTerminator(*result.addRegion(), builder, result.location);
  result.attributes.push_back(builder.getNamedAttr(
      mlir::SymbolTable::getSymbolAttrName(), builder.getStringAttr(name)));
}

static LogicalResult verifyModuleOp(ModuleOp op) {
  // TODO(benvanik): check export name conflicts.
  return success();
}

static ParseResult parseFuncOp(OpAsmParser &parser, OperationState *result) {
  auto buildFuncType = [](Builder &builder, ArrayRef<Type> argTypes,
                          ArrayRef<Type> results,
                          function_like_impl::VariadicFlag, std::string &) {
    return builder.getFunctionType(argTypes, results);
  };
  return function_like_impl::parseFunctionLikeOp(
      parser, *result, /*allowVariadic=*/false, buildFuncType);
}

static void printFuncOp(OpAsmPrinter &p, FuncOp &op) {
  FunctionType fnType = op.getType();
  function_like_impl::printFunctionLikeOp(
      p, op, fnType.getInputs(), /*isVariadic=*/false, fnType.getResults());
}

void FuncOp::build(OpBuilder &builder, OperationState &result, StringRef name,
                   FunctionType type, ArrayRef<NamedAttribute> attrs,
                   ArrayRef<DictionaryAttr> argAttrs) {
  result.addRegion();
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder.getStringAttr(name));
  result.addAttribute("type", TypeAttr::get(type));
  result.attributes.append(attrs.begin(), attrs.end());
  if (argAttrs.empty()) {
    return;
  }

  assert(type.getNumInputs() == argAttrs.size() &&
         "expected as many argument attribute lists as arguments");
  function_like_impl::addArgAndResultAttrs(builder, result, argAttrs,
                                           /*resultAttrs=*/llvm::None);
}

Block *FuncOp::addEntryBlock() {
  assert(empty() && "function already has an entry block");
  auto *entry = new Block();
  push_back(entry);
  entry->addArguments(getType().getInputs());
  return entry;
}

LogicalResult FuncOp::verifyType() {
  auto type = getTypeAttr().getValue();
  if (!type.isa<FunctionType>())
    return emitOpError("requires '" + getTypeAttrName() +
                       "' attribute of function type");
  return success();
}

void FuncOp::setReflectionAttr(StringRef name, Attribute value) {
  // TODO(benvanik): remove reflection attrs as a concept and use something more
  // MLIRish like an attribute interface/dialect interface.
  // DictionaryAttr is not very friendly for modification :/
  auto existingAttr =
      getOperation()->getAttrOfType<DictionaryAttr>("iree.reflection");
  SmallVector<NamedAttribute> attrs(existingAttr.begin(), existingAttr.end());
  bool didFind = false;
  for (size_t i = 0; i < attrs.size(); ++i) {
    if (attrs[i].first == name) {
      attrs[i].second = value;
      didFind = true;
      break;
    }
  }
  if (!didFind) {
    attrs.push_back(NamedAttribute(Identifier::get(name, getContext()), value));
    DictionaryAttr::sortInPlace(attrs);
  }
  getOperation()->setAttr("iree.reflection",
                          DictionaryAttr::getWithSorted(getContext(), attrs));
}

static ParseResult parseExportOp(OpAsmParser &parser, OperationState *result) {
  FlatSymbolRefAttr functionRefAttr;
  if (failed(parser.parseAttribute(functionRefAttr, "function_ref",
                                   result->attributes))) {
    return failure();
  }

  if (succeeded(parser.parseOptionalKeyword("as"))) {
    StringAttr exportNameAttr;
    if (failed(parser.parseLParen()) ||
        failed(parser.parseAttribute(exportNameAttr, "export_name",
                                     result->attributes)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  } else {
    result->addAttribute("export_name", parser.getBuilder().getStringAttr(
                                            functionRefAttr.getValue()));
  }

  if (failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }

  return success();
}

static void printExportOp(OpAsmPrinter &p, ExportOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.function_ref());
  if (op.export_name() != op.function_ref()) {
    p << " as(\"" << op.export_name() << "\")";
  }
  p.printOptionalAttrDictWithKeyword(
      op->getAttrs(), /*elidedAttrs=*/{"function_ref", "export_name"});
}

void ExportOp::build(OpBuilder &builder, OperationState &result,
                     FuncOp functionRef, StringRef exportName,
                     ArrayRef<NamedAttribute> attrs) {
  build(builder, result, builder.getSymbolRefAttr(functionRef),
        exportName.empty() ? functionRef.getName() : exportName, attrs);
}

void ExportOp::build(OpBuilder &builder, OperationState &result,
                     FlatSymbolRefAttr functionRef, StringRef exportName,
                     ArrayRef<NamedAttribute> attrs) {
  result.addAttribute("function_ref", functionRef);
  result.addAttribute("export_name", builder.getStringAttr(exportName));
  result.attributes.append(attrs.begin(), attrs.end());
}

static ParseResult parseImportOp(OpAsmParser &parser, OperationState *result) {
  auto builder = parser.getBuilder();
  StringAttr nameAttr;
  if (failed(parser.parseSymbolName(nameAttr,
                                    mlir::SymbolTable::getSymbolAttrName(),
                                    result->attributes)) ||
      failed(parser.parseLParen())) {
    return parser.emitError(parser.getNameLoc()) << "invalid import name";
  }
  SmallVector<DictionaryAttr, 8> argAttrs;
  SmallVector<Type, 8> argTypes;
  while (failed(parser.parseOptionalRParen())) {
    OpAsmParser::OperandType operand;
    Type operandType;
    auto operandLoc = parser.getCurrentLocation();
    if (failed(parser.parseOperand(operand)) ||
        failed(parser.parseColonType(operandType))) {
      return parser.emitError(operandLoc) << "invalid operand";
    }
    argTypes.push_back(operandType);
    NamedAttrList argAttrList;
    operand.name.consume_front("%");
    argAttrList.set("vm.name", builder.getStringAttr(operand.name));
    if (succeeded(parser.parseOptionalEllipsis())) {
      argAttrList.set("vm.variadic", builder.getUnitAttr());
    }
    argAttrs.push_back(argAttrList.getDictionary(result->getContext()));
    if (failed(parser.parseOptionalComma())) {
      if (failed(parser.parseRParen())) {
        return parser.emitError(parser.getCurrentLocation())
               << "invalid argument list (expected rparen)";
      }
      break;
    }
  }
  SmallVector<Type, 8> resultTypes;
  if (failed(parser.parseOptionalArrowTypeList(resultTypes))) {
    return parser.emitError(parser.getCurrentLocation())
           << "invalid result type list";
  }
  function_like_impl::addArgAndResultAttrs(builder, *result, argAttrs,
                                           /*resultAttrs=*/llvm::None);
  if (failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }

  auto functionType =
      FunctionType::get(result->getContext(), argTypes, resultTypes);
  result->addAttribute(mlir::function_like_impl::getTypeAttrName(),
                       TypeAttr::get(functionType));

  // No clue why this is required.
  result->addRegion();

  return success();
}

static void printImportOp(OpAsmPrinter &p, ImportOp &op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.getName());
  p << "(";
  for (int i = 0; i < op.getNumFuncArguments(); ++i) {
    if (auto name = op.getArgAttrOfType<StringAttr>(i, "vm.name")) {
      p << '%' << name.getValue() << " : ";
    }
    p.printType(op.getType().getInput(i));
    if (op.getArgAttrOfType<UnitAttr>(i, "vm.variadic")) {
      p << " ...";
    }
    if (i < op.getNumFuncArguments() - 1) {
      p << ", ";
    }
  }
  p << ")";
  if (op.getNumFuncResults() == 1) {
    p << " -> ";
    p.printType(op.getType().getResult(0));
  } else if (op.getNumFuncResults() > 1) {
    p << " -> (" << op.getType().getResults() << ")";
  }
  mlir::function_like_impl::printFunctionAttributes(
      p, op, op.getNumFuncArguments(), op.getNumFuncResults(),
      /*elided=*/
      {
          "is_variadic",
      });
}

void ImportOp::build(OpBuilder &builder, OperationState &result, StringRef name,
                     FunctionType type, ArrayRef<NamedAttribute> attrs,
                     ArrayRef<DictionaryAttr> argAttrs) {
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder.getStringAttr(name));
  result.addAttribute("type", TypeAttr::get(type));
  result.attributes.append(attrs.begin(), attrs.end());
  if (argAttrs.empty()) {
    return;
  }

  assert(type.getNumInputs() == argAttrs.size() &&
         "expected as many argument attribute lists as arguments");
  function_like_impl::addArgAndResultAttrs(builder, result, argAttrs,
                                           /*resultAttrs=*/llvm::None);
}

LogicalResult ImportOp::verifyType() {
  auto type = getTypeAttr().getValue();
  if (!type.isa<FunctionType>())
    return emitOpError("requires '" + getTypeAttrName() +
                       "' attribute of function type");
  return success();
}

//===----------------------------------------------------------------------===//
// Globals
//===----------------------------------------------------------------------===//

static LogicalResult verifyGlobalOp(Operation *op) {
  auto globalName =
      op->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
  auto globalType = op->getAttrOfType<TypeAttr>("type");
  auto initializerAttr = op->getAttrOfType<FlatSymbolRefAttr>("initializer");
  auto initialValueAttr = op->getAttr("initial_value");
  if (initializerAttr && initialValueAttr) {
    return op->emitOpError()
           << "globals can have either an initializer or an initial value";
  } else if (initializerAttr) {
    // Ensure initializer returns the same value as the global.
    auto initializer = op->getParentOfType<ModuleOp>().lookupSymbol<FuncOp>(
        initializerAttr.getValue());
    if (!initializer) {
      return op->emitOpError()
             << "initializer function " << initializerAttr << " not found";
    }
    if (initializer.getType().getNumInputs() != 0 ||
        initializer.getType().getNumResults() != 1 ||
        initializer.getType().getResult(0) != globalType.getValue()) {
      return op->emitOpError()
             << "initializer type mismatch; global " << globalName << " is "
             << globalType << " but initializer function "
             << initializer.getName() << " is " << initializer.getType();
    }
  } else if (initialValueAttr) {
    // Ensure the value is something we can convert to a const.
    if (initialValueAttr.getType() != globalType.getValue()) {
      return op->emitOpError()
             << "initial value type mismatch; global " << globalName << " is "
             << globalType << " but initial value provided is "
             << initialValueAttr.getType();
    }
  }
  return success();
}

static LogicalResult verifyGlobalAddressOp(GlobalAddressOp op) {
  auto *globalOp =
      op->getParentOfType<VM::ModuleOp>().lookupSymbol(op.global());
  if (!globalOp) {
    return op.emitOpError() << "Undefined global: " << op.global();
  }
  return success();
}

template <typename T>
static void addMemoryEffectsForGlobal(
    Operation *op, StringRef global,
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  // HACK: works around the lack of symbol side effects in mlir by only saying
  // we have a side-effect if the variable we are loading is mutable.
  auto *symbolOp = SymbolTable::lookupNearestSymbolFrom(op, global);
  assert(symbolOp);
  auto globalOp = dyn_cast<T>(symbolOp);
  if (globalOp.is_mutable()) {
    effects.emplace_back(MemoryEffects::Read::get());
  }
}

void GlobalLoadI32Op::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  addMemoryEffectsForGlobal<GlobalI32Op>(*this, global(), effects);
}

void GlobalLoadI64Op::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  addMemoryEffectsForGlobal<GlobalI64Op>(*this, global(), effects);
}

void GlobalLoadF32Op::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  addMemoryEffectsForGlobal<GlobalF32Op>(*this, global(), effects);
}

void GlobalLoadF64Op::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  addMemoryEffectsForGlobal<GlobalF64Op>(*this, global(), effects);
}

void GlobalLoadRefOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  addMemoryEffectsForGlobal<GlobalRefOp>(*this, global(), effects);
}

static LogicalResult verifyGlobalLoadOp(Operation *op) {
  auto globalAttr = op->getAttrOfType<FlatSymbolRefAttr>("global");
  auto *globalOp =
      op->getParentOfType<VM::ModuleOp>().lookupSymbol(globalAttr.getValue());
  if (!globalOp) {
    return op->emitOpError() << "Undefined global: " << globalAttr;
  }
  auto globalType = globalOp->getAttrOfType<TypeAttr>("type");
  auto loadType = op->getResult(0).getType();
  if (globalType.getValue() != loadType) {
    return op->emitOpError()
           << "Global type mismatch; global " << globalAttr << " is "
           << globalType << " but load is " << loadType;
  }
  return success();
}

static LogicalResult verifyGlobalStoreOp(Operation *op) {
  auto globalAttr = op->getAttrOfType<FlatSymbolRefAttr>("global");
  auto *globalOp =
      op->getParentOfType<VM::ModuleOp>().lookupSymbol(globalAttr.getValue());
  if (!globalOp) {
    return op->emitOpError() << "Undefined global: " << globalAttr;
  }
  auto globalType = globalOp->getAttrOfType<TypeAttr>("type");
  auto storeType = op->getOperand(0).getType();
  if (globalType.getValue() != storeType) {
    return op->emitOpError()
           << "Global type mismatch; global " << globalAttr << " is "
           << globalType << " but store is " << storeType;
  }
  if (!globalOp->getAttrOfType<UnitAttr>("is_mutable")) {
    return op->emitOpError() << "Global " << globalAttr
                             << " is not mutable and cannot be stored to";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// Constants
//===----------------------------------------------------------------------===//

template <typename T>
static ParseResult parseConstOp(OpAsmParser &parser, OperationState *result) {
  Attribute valueAttr;
  NamedAttrList dummyAttrs;
  if (failed(parser.parseAttribute(valueAttr, "value", dummyAttrs))) {
    return parser.emitError(parser.getCurrentLocation())
           << "Invalid attribute encoding";
  }
  if (!T::isBuildableWith(valueAttr, valueAttr.getType())) {
    return parser.emitError(parser.getCurrentLocation())
           << "Incompatible type or invalid type value formatting";
  }
  valueAttr = T::convertConstValue(valueAttr);
  result->addAttribute("value", valueAttr);
  if (failed(parser.parseOptionalAttrDict(result->attributes))) {
    return parser.emitError(parser.getCurrentLocation())
           << "Failed to parse optional attribute dict";
  }
  return parser.addTypeToList(valueAttr.getType(), result->types);
}

template <typename T>
static void printConstOp(OpAsmPrinter &p, T &op) {
  p << op.getOperationName() << ' ';
  p.printAttribute(op.value());
  p.printOptionalAttrDict(op->getAttrs(), /*elidedAttrs=*/{"value"});
}

template <int SZ>
static bool isConstIntegerBuildableWith(Attribute value, Type type) {
  // FlatSymbolRefAttr can only be used with a function type.
  if (value.isa<FlatSymbolRefAttr>()) {
    return false;
  }
  // Otherwise, the attribute must have the same type as 'type'.
  if (value.getType() != type) {
    return false;
  }
  if (value.isa<UnitAttr>()) {
    return SZ == 32;  // Conditions/bools are always i32
  } else if (auto intAttr = value.dyn_cast<IntegerAttr>()) {
    return intAttr.getType().isInteger(SZ);
  } else if (auto elementsAttr = value.dyn_cast<ElementsAttr>()) {
    return elementsAttr.getType().getElementType().isInteger(SZ);
  }
  return false;
}

template <int SZ>
static bool isConstFloatBuildableWith(Attribute value, Type type) {
  // FlatSymbolRefAttr can only be used with a function type.
  if (value.isa<FlatSymbolRefAttr>()) {
    return false;
  }
  // Otherwise, the attribute must have the same type as 'type'.
  if (value.getType() != type) {
    return false;
  }
  Type elementType;
  if (auto floatAttr = value.dyn_cast<FloatAttr>()) {
    elementType = floatAttr.getType();
  } else if (auto elementsAttr = value.dyn_cast<ElementsAttr>()) {
    elementType = elementsAttr.getType().getElementType();
  }
  if (!elementType) return false;
  return elementType.getIntOrFloatBitWidth() == SZ;
}

template <int SZ>
static Attribute convertConstIntegerValue(Attribute value) {
  assert(isConstIntegerBuildableWith<SZ>(value, value.getType()));
  Builder builder(value.getContext());
  auto integerType = builder.getIntegerType(SZ);
  int32_t dims = 1;
  if (value.isa<UnitAttr>()) {
    return IntegerAttr::get(integerType, APInt(SZ, 1));
  } else if (auto v = value.dyn_cast<BoolAttr>()) {
    return IntegerAttr::get(integerType,
                            APInt(SZ, v.getValue() ? 1 : 0, false));
  } else if (auto v = value.dyn_cast<IntegerAttr>()) {
    return IntegerAttr::get(integerType,
                            APInt(SZ, v.getValue().getLimitedValue()));
  } else if (auto v = value.dyn_cast<ElementsAttr>()) {
    dims = v.getNumElements();
    ShapedType adjustedType = VectorType::get({dims}, integerType);
    if (auto elements = v.dyn_cast<SplatElementsAttr>()) {
      return SplatElementsAttr::get(adjustedType, elements.getSplatValue());
    } else {
      return DenseElementsAttr::get(
          adjustedType, llvm::to_vector<4>(v.getValues<Attribute>()));
    }
  }
  llvm_unreachable("unexpected attribute type");
  return Attribute();
}

static FloatType getFloatType(int bitwidth, MLIRContext *context) {
  switch (bitwidth) {
    case 16:
      return FloatType::getF16(context);
    case 32:
      return FloatType::getF32(context);
    case 64:
      return FloatType::getF64(context);
    default:
      llvm_unreachable("unhandled floating point type");
      return {};
  }
}

template <int SZ>
static Attribute convertConstFloatValue(Attribute value) {
  assert(isConstFloatBuildableWith<SZ>(value, value.getType()));
  Builder builder(value.getContext());
  auto floatType = getFloatType(SZ, value.getContext());
  int32_t dims = 1;
  if (auto v = value.dyn_cast<FloatAttr>()) {
    return FloatAttr::get(floatType, v.getValue());
  } else if (auto v = value.dyn_cast<ElementsAttr>()) {
    dims = v.getNumElements();
    ShapedType adjustedType = VectorType::get({dims}, floatType);
    if (auto elements = v.dyn_cast<SplatElementsAttr>()) {
      return SplatElementsAttr::get(adjustedType, elements.getSplatValue());
    } else {
      return DenseElementsAttr::get(
          adjustedType, llvm::to_vector<4>(v.getValues<Attribute>()));
    }
  }
  llvm_unreachable("unexpected attribute type");
  return Attribute();
}

// static
bool ConstI32Op::isBuildableWith(Attribute value, Type type) {
  return isConstIntegerBuildableWith<32>(value, type);
}

// static
Attribute ConstI32Op::convertConstValue(Attribute value) {
  return convertConstIntegerValue<32>(value);
}

void ConstI32Op::build(OpBuilder &builder, OperationState &result,
                       Attribute value) {
  Attribute newValue = convertConstValue(value);
  result.addAttribute("value", newValue);
  result.addTypes(newValue.getType());
}

void ConstI32Op::build(OpBuilder &builder, OperationState &result,
                       int32_t value) {
  return build(builder, result, builder.getI32IntegerAttr(value));
}

// static
bool ConstI64Op::isBuildableWith(Attribute value, Type type) {
  return isConstIntegerBuildableWith<64>(value, type);
}

// static
Attribute ConstI64Op::convertConstValue(Attribute value) {
  return convertConstIntegerValue<64>(value);
}

void ConstI64Op::build(OpBuilder &builder, OperationState &result,
                       Attribute value) {
  Attribute newValue = convertConstValue(value);
  result.addAttribute("value", newValue);
  result.addTypes(newValue.getType());
}

void ConstI64Op::build(OpBuilder &builder, OperationState &result,
                       int64_t value) {
  return build(builder, result, builder.getI64IntegerAttr(value));
}

// static
bool ConstF32Op::isBuildableWith(Attribute value, Type type) {
  return isConstFloatBuildableWith<32>(value, type);
}

// static
Attribute ConstF32Op::convertConstValue(Attribute value) {
  return convertConstFloatValue<32>(value);
}

void ConstF32Op::build(OpBuilder &builder, OperationState &result,
                       Attribute value) {
  Attribute newValue = convertConstValue(value);
  result.addAttribute("value", newValue);
  result.addTypes(newValue.getType());
}

void ConstF32Op::build(OpBuilder &builder, OperationState &result,
                       float value) {
  return build(builder, result, builder.getF32FloatAttr(value));
}

// static
bool ConstF64Op::isBuildableWith(Attribute value, Type type) {
  return isConstFloatBuildableWith<64>(value, type);
}

// static
Attribute ConstF64Op::convertConstValue(Attribute value) {
  return convertConstFloatValue<64>(value);
}

void ConstF64Op::build(OpBuilder &builder, OperationState &result,
                       Attribute value) {
  Attribute newValue = convertConstValue(value);
  result.addAttribute("value", newValue);
  result.addTypes(newValue.getType());
}

void ConstF64Op::build(OpBuilder &builder, OperationState &result,
                       double value) {
  return build(builder, result, builder.getF64FloatAttr(value));
}

void ConstI32ZeroOp::build(OpBuilder &builder, OperationState &result) {
  result.addTypes(builder.getIntegerType(32));
}

void ConstI64ZeroOp::build(OpBuilder &builder, OperationState &result) {
  result.addTypes(builder.getIntegerType(64));
}

void ConstF32ZeroOp::build(OpBuilder &builder, OperationState &result) {
  result.addTypes(builder.getF32Type());
}

void ConstF64ZeroOp::build(OpBuilder &builder, OperationState &result) {
  result.addTypes(builder.getF64Type());
}

void ConstRefZeroOp::build(OpBuilder &builder, OperationState &result,
                           Type objectType) {
  result.addTypes(objectType);
}

void RodataOp::build(OpBuilder &builder, OperationState &result, StringRef name,
                     ElementsAttr value, ArrayRef<NamedAttribute> attrs) {
  result.addAttribute("sym_name", builder.getStringAttr(name));
  result.addAttribute("value", value);
  result.addAttributes(attrs);
}

static LogicalResult verifyConstRefRodataOp(ConstRefRodataOp &op) {
  auto *rodataOp =
      op->getParentOfType<VM::ModuleOp>().lookupSymbol(op.rodata());
  if (!rodataOp) {
    return op.emitOpError() << "Undefined rodata section: " << op.rodata();
  }
  return success();
}

void ConstRefRodataOp::build(OpBuilder &builder, OperationState &result,
                             StringRef rodataName,
                             ArrayRef<NamedAttribute> attrs) {
  result.addAttribute("rodata", builder.getSymbolRefAttr(rodataName));
  auto type =
      IREE::VM::RefType::get(IREE::VM::BufferType::get(builder.getContext()));
  result.addTypes({type});
  result.addAttributes(attrs);
}

void ConstRefRodataOp::build(OpBuilder &builder, OperationState &result,
                             RodataOp rodataOp,
                             ArrayRef<NamedAttribute> attrs) {
  build(builder, result, rodataOp.getName(), attrs);
}

//===----------------------------------------------------------------------===//
// Lists
//===----------------------------------------------------------------------===//

static LogicalResult verifyListGetRefOp(ListGetRefOp &op) {
  auto listType = op.list()
                      .getType()
                      .cast<IREE::VM::RefType>()
                      .getObjectType()
                      .cast<IREE::VM::ListType>();
  auto elementType = listType.getElementType();
  auto resultType = op.result().getType();
  if (!elementType.isa<IREE::VM::OpaqueType>()) {
    if (elementType.isa<IREE::VM::RefType>() !=
        resultType.isa<IREE::VM::RefType>()) {
      // Attempting to go between a primitive type and ref type.
      return op.emitError() << "cannot convert between list type "
                            << elementType << " and result type " << resultType;
    } else if (auto refType = elementType.dyn_cast<IREE::VM::RefType>()) {
      if (!refType.getObjectType().isa<IREE::VM::OpaqueType>() &&
          elementType != resultType) {
        // List has a concrete type, verify it matches.
        return op.emitError() << "list contains " << elementType
                              << " that cannot be accessed as " << resultType;
      }
    }
  }
  return success();
}

static LogicalResult verifyListSetRefOp(ListSetRefOp &op) {
  auto listType = op.list()
                      .getType()
                      .cast<IREE::VM::RefType>()
                      .getObjectType()
                      .cast<IREE::VM::ListType>();
  auto elementType = listType.getElementType();
  auto valueType = op.value().getType();
  if (!elementType.isa<IREE::VM::OpaqueType>()) {
    if (elementType.isa<IREE::VM::RefType>() !=
        valueType.isa<IREE::VM::RefType>()) {
      // Attempting to go between a primitive type and ref type.
      return op.emitError() << "cannot convert between list type "
                            << elementType << " and value type " << valueType;
    } else if (auto refType = elementType.dyn_cast<IREE::VM::RefType>()) {
      if (!refType.getObjectType().isa<IREE::VM::OpaqueType>() &&
          elementType != valueType) {
        // List has a concrete type, verify it matches.
        return op.emitError() << "list contains " << elementType
                              << " that cannot be mutated as " << valueType;
      }
    }
  }
  return success();
}

//===----------------------------------------------------------------------===//
// Assignment
//===----------------------------------------------------------------------===//

static ParseResult parseSwitchOp(OpAsmParser &parser, OperationState *result) {
  SmallVector<OpAsmParser::OperandType, 4> values;
  OpAsmParser::OperandType index;
  OpAsmParser::OperandType defaultValue;
  Type type;
  if (failed(parser.parseOperand(index)) ||
      failed(parser.parseOperandList(values, OpAsmParser::Delimiter::Square)) ||
      failed(parser.parseKeyword("else")) ||
      failed(parser.parseOperand(defaultValue)) ||
      failed(parser.parseOptionalAttrDict(result->attributes)) ||
      failed(parser.parseColonType(type)) ||
      failed(parser.resolveOperand(index,
                                   IntegerType::get(result->getContext(), 32),
                                   result->operands)) ||
      failed(parser.resolveOperand(defaultValue, type, result->operands)) ||
      failed(parser.resolveOperands(values, type, result->operands)) ||
      failed(parser.addTypeToList(type, result->types))) {
    return failure();
  }
  return success();
}

template <typename T>
static void printSwitchOp(OpAsmPrinter &p, T &op) {
  p << op.getOperationName() << " ";
  p.printOperand(op.index());
  p << "[";
  p.printOperands(op.values());
  p << "]";
  p << " else ";
  p.printOperand(op.default_value());
  p.printOptionalAttrDict(op->getAttrs());
  p << " : ";
  p.printType(op.default_value().getType());
}

static ParseResult parseSwitchRefOp(OpAsmParser &parser,
                                    OperationState *result) {
  return parseSwitchOp(parser, result);
}

static void printSwitchRefOp(OpAsmPrinter &p, SwitchRefOp &op) {
  printSwitchOp(p, op);
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

Block *BranchOp::getDest() { return getOperation()->getSuccessor(0); }

void BranchOp::setDest(Block *block) {
  return getOperation()->setSuccessor(block, 0);
}

void BranchOp::eraseOperand(unsigned index) {
  getOperation()->eraseOperand(index);
}

Optional<MutableOperandRange> BranchOp::getMutableSuccessorOperands(
    unsigned index) {
  assert(index == 0 && "invalid successor index");
  return destOperandsMutable();
}

static ParseResult parseCallVariadicOp(OpAsmParser &parser,
                                       OperationState *result) {
  FlatSymbolRefAttr calleeAttr;
  auto calleeLoc = parser.getNameLoc();
  if (failed(parser.parseAttribute(calleeAttr, "callee", result->attributes)) ||
      failed(parser.parseLParen())) {
    return parser.emitError(calleeLoc) << "invalid callee symbol";
  }

  // Parsing here is a bit tricky as we want to be able to support things like
  // variadic lists of tuples while we don't know that the types are tuples yet.
  // We'll instead parse each segment as a flat list so `[(%a, %b), (%c, %d)]`
  // parses as `[%a, %b, %c, %d]` and then do the accounting below when parsing
  // types.
  SmallVector<OpAsmParser::OperandType, 4> flatOperands;
  SmallVector<int16_t, 4> flatSegmentSizes;
  while (failed(parser.parseOptionalRParen())) {
    if (succeeded(parser.parseOptionalLSquare())) {
      // Variadic list.
      SmallVector<OpAsmParser::OperandType, 4> flatSegmentOperands;
      while (failed(parser.parseOptionalRSquare())) {
        if (succeeded(parser.parseOptionalLParen())) {
          // List contains tuples, so track the () and parse inside of it.
          while (failed(parser.parseOptionalRParen())) {
            OpAsmParser::OperandType segmentOperand;
            if (failed(parser.parseOperand(segmentOperand))) {
              return parser.emitError(parser.getCurrentLocation())
                     << "invalid operand";
            }
            flatSegmentOperands.push_back(segmentOperand);
            if (failed(parser.parseOptionalComma())) {
              if (failed(parser.parseRParen())) {
                return parser.emitError(parser.getCurrentLocation())
                       << "malformed nested variadic tuple operand list";
              }
              break;
            }
          }
        } else {
          // Flat list of operands.
          OpAsmParser::OperandType segmentOperand;
          if (failed(parser.parseOperand(segmentOperand))) {
            return parser.emitError(parser.getCurrentLocation())
                   << "invalid operand";
          }
          flatSegmentOperands.push_back(segmentOperand);
        }
        if (failed(parser.parseOptionalComma())) {
          if (failed(parser.parseRSquare())) {
            return parser.emitError(parser.getCurrentLocation())
                   << "malformed variadic operand list";
          }
          break;
        }
      }
      flatSegmentSizes.push_back(flatSegmentOperands.size());
      flatOperands.append(flatSegmentOperands.begin(),
                          flatSegmentOperands.end());
    } else {
      // Normal single operand.
      OpAsmParser::OperandType operand;
      if (failed(parser.parseOperand(operand))) {
        return parser.emitError(parser.getCurrentLocation())
               << "malformed non-variadic operand";
      }
      flatSegmentSizes.push_back(-1);
      flatOperands.push_back(operand);
    }
    if (failed(parser.parseOptionalComma())) {
      if (failed(parser.parseRParen())) {
        return parser.emitError(parser.getCurrentLocation())
               << "expected closing )";
      }
      break;
    }
  }

  if (failed(parser.parseOptionalAttrDict(result->attributes)) ||
      failed(parser.parseColon()) || failed(parser.parseLParen())) {
    return parser.emitError(parser.getCurrentLocation())
           << "malformed optional attributes list";
  }
  SmallVector<Type, 4> flatOperandTypes;
  SmallVector<Type, 4> segmentTypes;
  SmallVector<int16_t, 4> segmentSizes;
  int segmentIndex = 0;
  while (failed(parser.parseOptionalRParen())) {
    Type operandType;
    if (failed(parser.parseType(operandType))) {
      return parser.emitError(parser.getCurrentLocation())
             << "invalid operand type";
    }
    bool isVariadic = succeeded(parser.parseOptionalEllipsis());
    if (isVariadic) {
      int flatSegmentSize = flatSegmentSizes[segmentIndex];
      if (auto tupleType = operandType.dyn_cast<TupleType>()) {
        for (int i = 0; i < flatSegmentSize / tupleType.size(); ++i) {
          for (auto type : tupleType) {
            flatOperandTypes.push_back(type);
          }
        }
        segmentSizes.push_back(flatSegmentSize / tupleType.size());
      } else {
        for (int i = 0; i < flatSegmentSize; ++i) {
          flatOperandTypes.push_back(operandType);
        }
        segmentSizes.push_back(flatSegmentSize);
      }
    } else {
      flatOperandTypes.push_back(operandType);
      segmentSizes.push_back(-1);
    }
    segmentTypes.push_back(operandType);
    ++segmentIndex;

    if (failed(parser.parseOptionalComma())) {
      if (failed(parser.parseRParen())) {
        return parser.emitError(parser.getCurrentLocation())
               << "expected closing )";
      }
      break;
    }
  }
  if (failed(parser.resolveOperands(flatOperands, flatOperandTypes, calleeLoc,
                                    result->operands))) {
    return parser.emitError(parser.getCurrentLocation())
           << "operands do not match type list";
  }
  result->addAttribute(
      "segment_sizes",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int64_t>(segmentSizes.size())},
                          parser.getBuilder().getIntegerType(16)),
          segmentSizes));
  result->addAttribute("segment_types",
                       parser.getBuilder().getArrayAttr(llvm::to_vector<4>(
                           llvm::map_range(segmentTypes, [&](Type type) {
                             return TypeAttr::get(type).cast<Attribute>();
                           }))));

  if (failed(parser.parseOptionalArrowTypeList(result->types))) {
    return parser.emitError(parser.getCurrentLocation())
           << "malformed function type results";
  }

  return success();
}

static void printCallVariadicOp(OpAsmPrinter &p, CallVariadicOp &op) {
  p << op.getOperationName() << ' ' << op->getAttr("callee") << '(';
  int operand = 0;
  llvm::interleaveComma(
      llvm::zip(op.segment_sizes(), op.segment_types()), p,
      [&](std::tuple<APInt, Attribute> segmentSizeType) {
        int segmentSize = std::get<0>(segmentSizeType).getSExtValue();
        Type segmentType =
            std::get<1>(segmentSizeType).cast<TypeAttr>().getValue();
        if (segmentSize == -1) {
          p.printOperand(op.getOperand(operand++));
        } else {
          p << '[';
          if (auto tupleType = segmentType.dyn_cast<TupleType>()) {
            for (size_t i = 0; i < segmentSize; ++i) {
              p << '(';
              SmallVector<Value, 4> tupleOperands;
              for (size_t j = 0; j < tupleType.size(); ++j) {
                tupleOperands.push_back(op.getOperand(operand++));
              }
              p << tupleOperands;
              p << ')';
              if (i < segmentSize - 1) p << ", ";
            }
          } else {
            SmallVector<Value, 4> segmentOperands;
            for (int i = 0; i < segmentSize; ++i) {
              segmentOperands.push_back(op.getOperand(operand++));
            }
            p << segmentOperands;
          }
          p << ']';
        }
      });
  p << ')';
  p.printOptionalAttrDict(op->getAttrs(), /*elidedAttrs=*/{
                              "callee",
                              "segment_sizes",
                              "segment_types",
                          });
  p << " : (";
  llvm::interleaveComma(
      llvm::zip(op.segment_sizes(), op.segment_types()), p,
      [&](std::tuple<APInt, Attribute> segmentSizeType) {
        int segmentSize = std::get<0>(segmentSizeType).getSExtValue();
        Type segmentType =
            std::get<1>(segmentSizeType).cast<TypeAttr>().getValue();
        if (segmentSize == -1) {
          p.printType(segmentType);
        } else {
          p.printType(segmentType);
          p << " ...";
        }
      });
  p << ")";
  if (op.getNumResults() == 1) {
    p << " -> " << op.getResult(0).getType();
  } else if (op.getNumResults() > 1) {
    p << " -> (" << op.getResultTypes() << ")";
  }
}

Optional<MutableOperandRange> CondBranchOp::getMutableSuccessorOperands(
    unsigned index) {
  assert(index < getNumSuccessors() && "invalid successor index");
  return index == trueIndex ? trueDestOperandsMutable()
                            : falseDestOperandsMutable();
}

template <typename T>
static LogicalResult verifyFailOp(T op) {
  APInt status;
  if (matchPattern(op.status(), m_ConstantInt(&status))) {
    if (status == 0) {
      return op.emitOpError() << "status is 0; expected to not be OK";
    }
  }
  return success();
}

static ParseResult parseCondFailOp(OpAsmParser &parser,
                                   OperationState *result) {
  // First operand is either 'condition' or 'status', both i32.
  OpAsmParser::OperandType condition;
  if (failed(parser.parseOperand(condition))) {
    return failure();
  }

  // First try looking for an operand after a comma. If no operand, keep track
  // of the already parsed comma to avoid checking for a comma later on.
  bool trailingComma = false;
  OpAsmParser::OperandType status = condition;
  if (succeeded(parser.parseOptionalComma()) &&
      !parser.parseOptionalOperand(status).hasValue()) {
    trailingComma = true;
  }

  StringAttr messageAttr;
  if ((trailingComma || succeeded(parser.parseOptionalComma())) &&
      failed(
          parser.parseAttribute(messageAttr, "message", result->attributes))) {
    return failure();
  }

  Type operandType = IntegerType::get(result->getContext(), 32);
  if (failed(parser.resolveOperand(condition, operandType, result->operands)) ||
      failed(parser.resolveOperand(status, operandType, result->operands))) {
    return failure();
  }

  return parser.parseOptionalAttrDict(result->attributes);
}

static void printCondFailOp(OpAsmPrinter &p, CondFailOp op) {
  p << op.getOperationName() << ' ';
  if (op.condition() != op.status()) {
    p << op.condition() << ", ";
  }
  p << op.status();
  if (op.message().hasValue()) {
    p << ", \"" << op.message().getValue() << "\"";
  }
  p.printOptionalAttrDict(op->getAttrs(), /*elidedAttrs=*/{"message"});
}

//===----------------------------------------------------------------------===//
// Async/fiber ops
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Debugging
//===----------------------------------------------------------------------===//

Block *BreakOp::getDest() { return getOperation()->getSuccessor(0); }

void BreakOp::setDest(Block *block) {
  return getOperation()->setSuccessor(block, 0);
}

void BreakOp::eraseOperand(unsigned index) {
  getOperation()->eraseOperand(index);
}

Optional<MutableOperandRange> BreakOp::getMutableSuccessorOperands(
    unsigned index) {
  assert(index == 0 && "invalid successor index");
  return destOperandsMutable();
}

Block *CondBreakOp::getDest() { return getOperation()->getSuccessor(0); }

void CondBreakOp::setDest(Block *block) {
  return getOperation()->setSuccessor(block, 0);
}

void CondBreakOp::eraseOperand(unsigned index) {
  getOperation()->eraseOperand(index);
}

Optional<MutableOperandRange> CondBreakOp::getMutableSuccessorOperands(
    unsigned index) {
  assert(index == 0 && "invalid successor index");
  return destOperandsMutable();
}

}  // namespace VM
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

//===----------------------------------------------------------------------===//
// TableGen definitions (intentionally last)
//===----------------------------------------------------------------------===//

#include "iree/compiler/Dialect/VM/IR/VMOpEncoder.cpp.inc"  // IWYU pragma: keep
#define GET_OP_CLASSES
#include "iree/compiler/Dialect/VM/IR/VMOps.cpp.inc"  // IWYU pragma: keep
