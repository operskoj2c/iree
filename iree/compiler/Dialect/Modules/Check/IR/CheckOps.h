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

#ifndef IREE_COMPILER_DIALECT_MODULES_CHECK_IR_CHECK_OPS_H_
#define IREE_COMPILER_DIALECT_MODULES_CHECK_IR_CHECK_OPS_H_

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"

#define GET_OP_CLASSES
#include "iree/compiler/Dialect/Modules/Check/IR/CheckOps.h.inc"

#endif  // IREE_COMPILER_DIALECT_MODULES_CHECK_IR_CHECK_OPS_H_
