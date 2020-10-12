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

//===- Utils.h - Utility functions used in Linalg to SPIR-V lowering ------===//
//
// Utility functions used while lowering from Linalg to SPIRV.
//
//===----------------------------------------------------------------------===//
#ifndef IREE_COMPILER_CONVERSION_LINALGTOSPIRV_UTILS_H_
#define IREE_COMPILER_CONVERSION_LINALGTOSPIRV_UTILS_H_

#include "mlir/Dialect/Linalg/Utils/Utils.h"
#include "mlir/Support/LLVM.h"

namespace mlir {
class FuncOp;
class Value;
class SubViewOp;
class OperationFolder;
class OpBuilder;
class LogicalResult;

namespace iree_compiler {

static constexpr int kNumGPUDims = 3;

/// Updates the workgroup size used for the dispatch region.
LogicalResult updateWorkGroupSize(FuncOp funcOp,
                                  ArrayRef<int64_t> workGroupSize);

/// Allocation callback for allocation workgroup local memory.
Optional<Value> allocateWorkgroupMemory(OpBuilder &b, SubViewOp subview,
                                        ArrayRef<Value> boundingSubViewSize,
                                        OperationFolder *folder);

/// Deallocation callback for allocation workgroup local memory.
LogicalResult deallocateWorkgroupMemory(OpBuilder &b, Value buffer);

/// Function used as callback for copyin/copyout in promotion pattern used
/// to promote subviews to workgroup memory when the number of threads is
/// known to be greater than equal to the number of iteration of loops the
/// copy is lowered to.
LogicalResult copyToWorkgroupMemory(OpBuilder &b, Value src, Value dst);

class GPUGlobalId;
class GPUGlobalCount;

/// Generate the operations that compute the processor ID and number of
/// processors. Used as the callback needed for LinalgDistributionOptions.
template <typename GPUIdOp, typename GPUCountOp>
SmallVector<linalg::ProcInfo, 2> getGPUProcessorIdsAndCounts(OpBuilder &builder,
                                                             Location loc,
                                                             unsigned numDims);
}  // namespace iree_compiler
}  // namespace mlir

#endif  //  IREE_COMPILER_CONVERSION_LINALGTOSPIRV_UTILS_H_
