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

#ifndef IREE_COMPILER_CONVERSION_LINALGTOSPIRV_PASSES_H_
#define IREE_COMPILER_CONVERSION_LINALGTOSPIRV_PASSES_H_

#include "iree/compiler/Conversion/LinalgToSPIRV/CodeGenOptionUtils.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

//===----------------------------------------------------------------------===//
// Passes
//===----------------------------------------------------------------------===//

/// Pass to tile and vectorize Linalg operations on buffers in a single
/// workgroup.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createTileAndVectorizeInOneWorkgroupPass(const SPIRVCodegenOptions &options);

/// Pass to add the synchronizations and attributes needed to lower from PLoops
/// to GPU dialect.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createConvertToGPUPass(const SPIRVCodegenOptions &options);

/// Pass to perform the final conversion to SPIR-V dialect.
/// This pass converts remaining interface ops into SPIR-V global variables,
/// GPU processor ID ops into SPIR-V global variables, loop/standard ops into
/// corresponding SPIR-V ops.
std::unique_ptr<OperationPass<ModuleOp>> createConvertToSPIRVPass();

/// Pass to split computation workload to multiple sequential dispatch
/// functions. This pass operates on Linalg ops and prepares for lowering to
/// GPU, where we need to tile the workload to workgroups and workitems. If the
/// workload involves computation A and B, where B is dependent on A and A needs
/// all workgroups to complete, then we need to split A and B into different
/// kernels because there is no mechanism to perform cross-workgroup
/// synchronization within a single kernel.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createSplitDispatchFunctionPass();

/// Pass to convert vector operations to GPU level operations. Instructions of
/// vector size equal to subgroup size are distributed across the subgroup.
std::unique_ptr<OperationPass<FuncOp>> createVectorToGPUPass();

/// Pass to apply tiling and vectorization transformations on linagl::MatMulOp.
std::unique_ptr<FunctionPass> createMatMulTileAndVectorizeGPUPass();

/// Converts memref of scalar to memref of vector of efficent size. This will
/// allow to convert memory accesses to vector load/store in SPIR-V without
/// having pointer bitcast.
std::unique_ptr<OperationPass<ModuleOp>> createVectorizeMemref();

/// Creates a pass to fold processor ID uses where possible.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createFoldProcessorIDUsesPass();

/// Pass that materializes new hal.executable.entry_point ops for
/// spv.EntryPoints that are added by other passes.
/// To be removed along with SplitDispatchFunctionPass.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createMaterializeEntryPointsPass();

/// Creates a pass to concretize hal.interface.workgroup.* ops with concrete
/// tiling and distribution scheme.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createConcretizeTileAmongWorkgroupsPass(const SPIRVCodegenOptions &options);

/// Tiles and distributes Linalg operations on buffers among multiple
/// workgroups.
std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createTileAndDistributeAmongWorkgroupsPass(const SPIRVCodegenOptions &options);

// Flattens n-D MemRef subspan ops to 1-D MemRef and folds the byte offsets on
// subspan ops to the consumer load/store ops, in preparation for lowering to
// SPIR-V.
std::unique_ptr<FunctionPass> createFlattenMemRefSubspanPass();

//===----------------------------------------------------------------------===//
// Pipelines
//===----------------------------------------------------------------------===//

/// Populates passes needed to lower a XLA HLO op to SPIR-V dialect via the
/// structured ops path. The pass manager `pm` in here operate on the module
/// within the IREE::HAL::ExecutableOp. The `workGroupSize` can be used to
/// control the work group size used in the code generation and is intended for
/// testing purposes only. The pass pipeline will set an appropriate workgroup
/// size.
void buildSPIRVTransformPassPipeline(OpPassManager &pm,
                                     const SPIRVCodegenOptions &options);

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

/// Populates patterns to tile and distribute linalg operations.
void populateLinalgTileAndDistributePatterns(
    MLIRContext *context, OwningRewritePatternList &patterns);

/// Populates patterns to fold processor ID uses by using processor counts
/// information where possible.
void populateFoldGPUProcessorIDUsesPatterns(MLIRContext *context,
                                            OwningRewritePatternList &patterns);

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_CONVERSION_LINALGTOSPIRV_PASSES_H_
