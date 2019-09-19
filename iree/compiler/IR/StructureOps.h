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

#ifndef IREE_COMPILER_IR_STRUCTUREOPS_H_
#define IREE_COMPILER_IR_STRUCTUREOPS_H_

#include <cstdint>

#include "iree/compiler/IR/Types.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Dialect/StandardOps/Ops.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Attributes.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Dialect.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/FunctionSupport.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Module.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/OpDefinition.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/StandardTypes.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {

#define GET_OP_CLASSES
#include "iree/compiler/IR/StructureOps.h.inc"

}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_IR_STRUCTUREOPS_H_
