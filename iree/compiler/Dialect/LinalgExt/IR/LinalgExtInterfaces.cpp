// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/LinalgExt/IR/LinalgExtInterfaces.h"

namespace mlir {
namespace iree_compiler {
namespace linalg_ext {

OpOperandVector::operator SmallVector<Value>() {
  SmallVector<Value> result;
  result.reserve(this->size());
  llvm::transform(*this, std::back_inserter(result),
                  [](OpOperand *opOperand) { return opOperand->get(); });
  return result;
}

namespace detail {
LogicalResult verifyLinalgExtOpInterface(Operation *op) {
  LinalgExtOp linalgExtOp = cast<LinalgExtOp>(op);
  if (op->getNumResults()) {
    if (!linalgExtOp.hasTensorSemantics()) {
      return linalgExtOp.emitOpError(
          "expected inputs and outputs to be RankedTensorType or scalar");
    }

    if (op->getNumResults() != linalgExtOp.outputs().size()) {
      return linalgExtOp.emitOpError(
          "expected number of outputs to be same as the number of results");
    }
    for (auto en : llvm::enumerate(op->getResultTypes())) {
      Type outputType = linalgExtOp.outputs()[en.index()].getType();
      if (en.value() != outputType) {
        return linalgExtOp.emitOpError("expected type of `outs` operand #")
               << en.index() << " " << outputType
               << " to be same as result type " << en.value();
      }
    }
  } else {
    if (!linalgExtOp.hasBufferSemantics()) {
      return linalgExtOp.emitOpError(
          "expected inputs and outputs to be MemRefType or scalar");
    }
  }
  return success();
}
}  // namespace detail

#include "iree/compiler/Dialect/LinalgExt/IR/LinalgExtInterfaces.cpp.inc"  // IWYU pragma: export
}  // namespace linalg_ext
}  // namespace iree_compiler
}  // namespace mlir
