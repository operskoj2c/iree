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

#include "iree/compiler/Dialect/IREE/IR/IREEDialect.h"

#include "iree/compiler/Dialect/IREE/IR/IREEOps.h"
#include "iree/compiler/Dialect/IREE/IR/IREETypes.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SourceMgr.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Parser.h"

namespace mlir {
namespace iree_compiler {

static DialectRegistration<IREEDialect> base_dialect;

IREEDialect::IREEDialect(MLIRContext* context)
    : Dialect(getDialectNamespace(), context) {
  addTypes<IREE::ByteBufferType, IREE::MutableByteBufferType,
           IREE::OpaqueRefObjectType, IREE::RefPtrType,
           IREE::RankedShapeType>();
#define GET_OP_LIST
  addOperations<
#include "iree/compiler/Dialect/IREE/IR/IREEOps.cpp.inc"
      >();
}

//===----------------------------------------------------------------------===//
// Type parsing and printing
//===----------------------------------------------------------------------===//

static Type parseRankedShape(DialectAsmParser& parser) {
  llvm::SmallVector<int64_t, 7> dims;
  Type dimType;
  if (parser.parseLess()) return nullptr;
  if (failed(parser.parseDimensionList(dims))) {
    return nullptr;
  }
  if (failed(parser.parseType(dimType))) {
    return nullptr;
  }
  if (failed(parser.parseGreater())) {
    parser.emitError(parser.getNameLoc(), "expected terminating '>'");
    return nullptr;
  }

  return IREE::RankedShapeType::getChecked(
      dims, dimType, parser.getEncodedSourceLoc(parser.getNameLoc()));
}

static void printRankedShape(IREE::RankedShapeType type,
                             DialectAsmPrinter& printer) {
  llvm::SmallVector<int64_t, 7> dims;
  type.getAllDims(dims);
  printer << "ranked_shape<";
  bool first = true;
  for (auto dim : dims) {
    if (first) {
      first = false;
    } else {
      printer << "x";
    }

    if (dim < 0)
      printer << "?";
    else
      printer << dim;
  }

  if (!first) printer << "x";
  printer.printType(type.getDimType());
  printer << ">";
}

Type IREEDialect::parseType(DialectAsmParser& parser) const {
  if (succeeded(parser.parseOptionalKeyword("ranked_shape"))) {
    return parseRankedShape(parser);
  }

  Location loc = parser.getEncodedSourceLoc(parser.getNameLoc());
  llvm::StringRef spec = parser.getFullSymbolSpec();
  if (spec.consume_front("ref")) {
    if (!spec.consume_front("<") || !spec.consume_back(">")) {
      parser.emitError(parser.getCurrentLocation())
          << "malformed ref_ptr type '" << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    auto objectType = mlir::parseType(spec, getContext());
    if (!objectType) {
      parser.emitError(parser.getCurrentLocation())
          << "invalid ref_ptr object type specification: '"
          << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    return IREE::RefPtrType::getChecked(objectType, loc);
  } else if (spec == "opaque_ref") {
    return IREE::RefPtrType::getChecked(
        IREE::OpaqueRefObjectType::get(getContext()), loc);
  } else if (spec == "byte_buffer_ref") {
    return IREE::RefPtrType::getChecked(IREE::ByteBufferType::get(getContext()),
                                        loc);
  } else if (spec == "mutable_byte_buffer_ref") {
    return IREE::RefPtrType::getChecked(
        IREE::MutableByteBufferType::get(getContext()), loc);
  } else if (spec == "opaque") {
    return IREE::OpaqueRefObjectType::get(getContext());
  } else if (spec == "byte_buffer") {
    return IREE::ByteBufferType::get(getContext());
  } else if (spec == "mutable_byte_buffer") {
    return IREE::MutableByteBufferType::get(getContext());
  }
  emitError(loc, "unknown IREE type: ") << spec;
  return Type();
}

void IREEDialect::printType(Type type, DialectAsmPrinter& os) const {
  switch (type.getKind()) {
    case IREE::TypeKind::RefPtr: {
      auto objectType = type.cast<IREE::RefPtrType>().getObjectType();
      if (objectType.isa<IREE::ByteBufferType>()) {
        os << "byte_buffer_ref";
      } else if (objectType.isa<IREE::MutableByteBufferType>()) {
        os << "mutable_byte_buffer_ref";
      } else if (objectType.isa<IREE::OpaqueRefObjectType>()) {
        os << "opaque_ref";
      } else {
        os << "ref<" << objectType << ">";
      }
      break;
    }
    case IREE::TypeKind::OpaqueRefObject:
      os << "opaque";
      break;
    case IREE::TypeKind::ByteBuffer:
      os << "byte_buffer";
      break;
    case IREE::TypeKind::MutableByteBuffer:
      os << "mutable_byte_buffer";
      break;
    case IREE::TypeKind::RankedShape:
      printRankedShape(type.cast<IREE::RankedShapeType>(), os);
      break;
    default:
      llvm_unreachable("unhandled IREE type");
  }
}

}  // namespace iree_compiler
}  // namespace mlir
