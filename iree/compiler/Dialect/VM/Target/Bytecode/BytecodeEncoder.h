// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_DIALECT_VM_TARGET_BYTECODE_BYTECODEENCODER_H_
#define IREE_COMPILER_DIALECT_VM_TARGET_BYTECODE_BYTECODEENCODER_H_

#include "iree/compiler/Dialect/VM/IR/VMFuncEncoder.h"
#include "iree/compiler/Dialect/VM/IR/VMOps.h"
#include "iree/compiler/Dialect/VM/Target/Bytecode/DebugDatabaseBuilder.h"
#include "mlir/IR/SymbolTable.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace VM {

struct EncodedBytecodeFunction {
  // Encoded bytecode data for the function body.
  std::vector<uint8_t> bytecodeData;

  // Total i32 register slots required for execution.
  // Note that larger types also use these slots (i64=2xi32).
  uint16_t i32RegisterCount = 0;
  // Total vm.ref register slots required for execution.
  uint16_t refRegisterCount = 0;
};

// Abstract encoder used for function bytecode encoding.
class BytecodeEncoder : public VMFuncEncoder {
 public:
  // Encodes a vm.func to bytecode and returns the result.
  // Returns None on failure.
  static Optional<EncodedBytecodeFunction> encodeFunction(
      IREE::VM::FuncOp funcOp, llvm::DenseMap<Type, int> &typeTable,
      SymbolTable &symbolTable, DebugDatabaseBuilder &debugDatabase);

  BytecodeEncoder() = default;
  ~BytecodeEncoder() = default;
};

}  // namespace VM
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_VM_TARGET_BYTECODE_BYTECODEENCODER_H_
