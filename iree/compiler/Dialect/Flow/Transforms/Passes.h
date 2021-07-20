// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_DIALECT_FLOW_TRANSFORMS_PASSES_H_
#define IREE_COMPILER_DIALECT_FLOW_TRANSFORMS_PASSES_H_

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "llvm/ADT/StringMap.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/LLVM.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

//===----------------------------------------------------------------------===//
// Pipelines
//===----------------------------------------------------------------------===//

// Adds a set of passes to the given pass manager that run the required flow
// transforms in the canonical order.
//
// Most translation code should prefer to use this instead of manually adding
// the passes themselves to ensure that expected pass ordering is observed.
//
// The expected usage is:
//   Input legalization by one of:
//     - Directly passing supported flow plus core ops
//   buildFlowTransformPassPipeline
//   <run conversion from flow to sequencer/hal/vm/etc>
void buildFlowTransformPassPipeline(OpPassManager &passManager);

void registerFlowTransformPassPipeline();

//===----------------------------------------------------------------------===//
// Input canonicalization and legalization
//===----------------------------------------------------------------------===//

/// Creates a pass to convert linalg convolution ops with 1x1 kernels into
/// linalg.matmul
std::unique_ptr<OperationPass<FuncOp>> createConvertConv2D1x1ToMatmulPass();

/// Creates a pass to convert linalg convolution ops into linalg.matmul ops
/// using im2col tranformation.
std::unique_ptr<OperationPass<FuncOp>> createConvertConv2DToImg2ColPass();

/// Pass to convert a linalg.pad_tensor operation into a linalg.fill +
/// subtensor_insert. This allows lowering the operation into a single kernel.
std::unique_ptr<Pass> createPadTensorToSubTensorInsertPass();

/// Creates a pass to fuse Linalg operations on tensors.
std::unique_ptr<Pass> createFusionOfTensorOpsPass();

/// Create a pass to interchange generic ops to force the reduction loop to be
/// the most inner loops.
std::unique_ptr<OperationPass<FuncOp>> createInterchangeGenericOpsPass();

// Convert operations to equivalent flow.tensor.* ops. This is run after
// dispatch region creation to catch operations that were left outside of
// dispatch regions and could be represented as flow.tensor.* ops.
std::unique_ptr<OperationPass<FuncOp>> createConvertToFlowTensorOpsPass();

// Promote I1 tensor constants to I8 tensors to match later operations.
std::unique_ptr<OperationPass<FuncOp>> createPromoteI1ToI8Pass();

// Converts standard ops which match to flow.tensor.load (typically causing a
// read-back).
// Note that there are typically very specific phase ordering issues with
// performing such a conversion, so even though it is of fine granularity,
// this is maintained separately.
std::unique_ptr<OperationPass<FuncOp>> createPromoteTensorLoadsPass();

// Expands dynamic !shapex.ranked_shape dimensions in variables.
std::unique_ptr<OperationPass<ModuleOp>> createExpandVariableDynamicDimsPass();

//===----------------------------------------------------------------------===//
// Dispatches (flow.dispatch.workgroups)
//===----------------------------------------------------------------------===//

/// Pass to perform dispatch of Linalg on tensor ops by tiling and distribution.
/// A dispatch region is created for each tiled loop nest.
std::unique_ptr<OperationPass<FuncOp>> createDispatchLinalgOnTensorsPass();

// Outlines dispatch regions into executables.
std::unique_ptr<OperationPass<ModuleOp>> createOutlineDispatchRegionsPass();

// Injects tracing markers for dispatch operation tensor inputs and outputs.
std::unique_ptr<OperationPass<FuncOp>> createInjectDispatchTracingPass();

// Exports all functions and dispatch executables as `() -> ()` benchmark funcs.
std::unique_ptr<OperationPass<ModuleOp>> createExportBenchmarkFuncsPass();

//===----------------------------------------------------------------------===//
// Linalg transforms
//===----------------------------------------------------------------------===//

/// A pass to pad linalg ops to the next integer multiple of `paddingSize`.
std::unique_ptr<OperationPass<FuncOp>> createPadLinalgOpsToIntegerMultiplePass(
    int paddingSize = 4);

//===----------------------------------------------------------------------===//
// Optimizations
//===----------------------------------------------------------------------===//

// Outlines large tensor constants into flow.variables at the module level.
//
// TODO(#5493): implement the support for inlining constants into the command
// buffer and raise this value to one that is measured to be good.
static constexpr size_t kMinLargeConstantSize = 1;
std::unique_ptr<OperationPass<ModuleOp>> createOutlineLargeConstantsPass(
    size_t minLargeConstantSize = kMinLargeConstantSize);

// Deduplicates equivalent executables.
std::unique_ptr<OperationPass<ModuleOp>> createDeduplicateExecutablesPass();

//===----------------------------------------------------------------------===//
// Stream Formation and Folding
//===----------------------------------------------------------------------===//

// Identifies dispatches that can be grouped into streams within functions.
std::unique_ptr<OperationPass<FuncOp>> createFormStreamsPass();

// Reorders blocks to hoist ops that cannot be put into streams.
std::unique_ptr<OperationPass<FuncOp>> createHoistUnstreamableOpsPass();

// TODO(benvanik): cross-function stream flows.

//===----------------------------------------------------------------------===//
// Module Analysis and Finalization
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Simplification and Development Tools
//===----------------------------------------------------------------------===//

// Strips constant flow.variables and replaces them with splats.
// This destructively removes data (often model weights and other parameters)
// and is intended for use as a development tool.
// TODO(scotttodd): pass pipeline with this and other development passes to
//                  generate test cases / models suitable for check-in
std::unique_ptr<OperationPass<ModuleOp>>
createStripAndSplatConstantVariablesPass();

//===----------------------------------------------------------------------===//
// Register all Passes
//===----------------------------------------------------------------------===//

void registerFlowPasses();

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_FLOW_TRANSFORMS_PASSES_H_
