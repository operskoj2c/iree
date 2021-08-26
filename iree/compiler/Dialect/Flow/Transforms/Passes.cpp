// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Flow/Transforms/Passes.h"

#include <memory>

#include "iree/compiler/Dialect/Shape/Transforms/Passes.h"
#include "iree/compiler/Dialect/Util/Transforms/Passes.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/MemRef/Transforms/Passes.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

// TODO(ravishankarm): Change to a pipeline option.
static llvm::cl::opt<bool> clExportBenchmarkFuncs(
    "iree-flow-export-benchmark-funcs",
    llvm::cl::desc(
        "Exports one function per original module entry point and "
        "unique flow.executable that dispatches with dummy arguments."),
    llvm::cl::init(false));

// TODO(ravishankarm): Change to a pipeline option.
static llvm::cl::opt<bool> clTraceDispatchTensors(
    "iree-flow-trace-dispatch-tensors2",
    llvm::cl::desc(
        "Trace runtime input/output tensors for each dispatch function."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> clDemoteF32ToF16(
    "iree-flow-demote-f32-to-f16",
    llvm::cl::desc("Convert all f32 ops and values into f16 counterparts "
                   "unconditionally before main flow conversions"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> clEnable1x1ConvToMatmul(
    "iree-flow-enable-1x1-conv-to-matmul",
    llvm::cl::desc("Enable converting 1x1 linalg convolution ops to linalg "
                   "matmul ops pass."),
    llvm::cl::init(true));

static llvm::cl::opt<bool> clEnableConvToImg2Col(
    "iree-flow-enable-conv-img2col-transform",
    llvm::cl::desc("Enable converting convolution ops to img2col form."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> clEnablePaddingLinalgOps(
    "iree-flow-enable-padding-linalg-ops",
    llvm::cl::desc("Enable padding linalg ops to an integer multiple of "
                   "flow-padding-size"),
    llvm::cl::init(false));

static llvm::cl::opt<int> clLinalgOpsPaddingSize(
    "iree-flow-linalg-ops-padding-size",
    llvm::cl::desc("Enable padding linalg ops to an integer multiple of "
                   "flow-padding-size"),
    llvm::cl::init(4));

static llvm::cl::opt<bool> clEnableMatmulToMMT4d(
    "iree-flow-enable-matmul-to-mmt4d",
    llvm::cl::desc("Enable converting linalg.matmul into linalg.mmt4d"),
    llvm::cl::init(false));

// TODO(#1159): enable by default or remove this option once it works on
//              a broader set of programs
static llvm::cl::opt<bool> clEnableLinalgDetensorize(
    "iree-flow-enable-linalg-detensorize",
    llvm::cl::desc("Enable detensorizing linalg ops to operate on primitives"),
    llvm::cl::init(false));

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

void buildFlowTransformPassPipeline(OpPassManager &passManager) {
  passManager.addNestedPass<mlir::FuncOp>(createVerifyInputLegalityPass());

  // Simplify util.global accesses early on; this can help with dispatch
  // region formation as redundant store-loads are removed.
  passManager.addNestedPass<mlir::FuncOp>(
      IREE::Util::createSimplifyGlobalAccessesPass());

  // Perform cleanup after variable simplification as more canonicalizers may be
  // able to kick in.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCSEPass());

  // Replaces variables with !shapex.ranked_shape types with individual
  // variables for each dimension. This allows for constant dimensions to be
  // DCE'd in following passes.
  passManager.addPass(createExpandGlobalDynamicDimsPass());

  // Materialize dynamic shapes in the IR, also expanding function signatures
  // such that:
  //   - Dynamic ranked tensors: (tensor<?x?xf32>) expands to
  //     (tensor<?x?xf32>, ranked_shape<[?,?]>), and ultimately expands to
  //     (tensor<?x?xf32>, i32, i32)
  //   - Unranked tensors: **unsupported**
  // The generated ABI wrappers assume such an expansion and will generate code
  // to produce it from the original reflection metadata captured in the
  // previous pass.
  passManager.addNestedPass<mlir::FuncOp>(
      Shape::createExpandFunctionDynamicDimsPass());

  // Special case peephole optimizations.
  {
    if (clEnable1x1ConvToMatmul) {
      passManager.addNestedPass<FuncOp>(createConvertConv2D1x1ToMatmulPass());
    }
    if (clEnableConvToImg2Col) {
      passManager.addNestedPass<FuncOp>(createConvertConv2DToImg2ColPass());
    }
    // Pad linalg op
    if (clEnablePaddingLinalgOps) {
      passManager.addNestedPass<FuncOp>(
          createPadLinalgOpsToIntegerMultiplePass(clLinalgOpsPaddingSize));
    }
    // Convert linalg.matmul -> linalg.mmt4d
    if (clEnableMatmulToMMT4d) {
      passManager.addNestedPass<FuncOp>(
          createConvertLinalgMatmulOpToLinalgMMT4dPass(clLinalgOpsPaddingSize,
                                                       clLinalgOpsPaddingSize,
                                                       clLinalgOpsPaddingSize));
    }
  }
  passManager.addPass(createPadTensorToSubTensorInsertPass());

  // Elementwise, fusion, tiling and distribution.
  passManager.addNestedPass<mlir::FuncOp>(
      mlir::createConvertElementwiseToLinalgPass());
  passManager.addNestedPass<mlir::FuncOp>(
      mlir::createLinalgFoldUnitExtentDimsPass());
  passManager.addNestedPass<mlir::FuncOp>(createInterchangeGenericOpsPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(createFusionOfTensorOpsPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCSEPass());
  if (clEnableLinalgDetensorize) {
    passManager.addNestedPass<mlir::FuncOp>(
        mlir::createLinalgDetensorizePass());
  }
  passManager.addPass(memref::createResolveShapedTypeResultDimsPass());
  passManager.addNestedPass<mlir::FuncOp>(
      createConvertToFlowBeforeDispatchFormation());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(createDispatchLinalgOnTensorsPass());
  passManager.addPass(memref::createResolveShapedTypeResultDimsPass());
  passManager.addNestedPass<mlir::FuncOp>(
      createConvertToFlowAfterDispatchFormation());
  // NOTE: required because the current dispatch-linalg-on-tensors pass
  // creates a lot of dead IR that needs to be cleaned up.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());

  // Outline the dispatch regions into their own functions wrapped in
  // executables.
  passManager.addPass(createOutlineDispatchRegionsPass());

  // Cleanup identity ops that clutter up the IR and canonicalize.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());

  // Deduplicate executables created from dispatch regions.
  // Note: this only deduplicates equivalent executables. We could in addition
  // generalize executables to prune further (e.g. by promoting a dimension to
  // an argument if two executables differ only in that one dimension).
  passManager.addPass(createDeduplicateExecutablesPass());

  // Create one function per remaining flow.executable that can be used with
  // iree-benchmark-module to benchmark each dispatch individually, as well as
  // exporting all original model entry points.
  if (clExportBenchmarkFuncs) {
    passManager.addPass(createExportBenchmarkFuncsPass());
  }

  // Inject tracing that logs both input and output tensors from all dispatches.
  // We do this after deduping so that the executable names match later stages.
  if (clTraceDispatchTensors) {
    passManager.addNestedPass<mlir::FuncOp>(createInjectDispatchTracingPass());
  }

  //----------------------------------------------------------------------------
  // Stream formation.
  // Pre-conditions:
  //   - Full formation of dispatch regions
  //----------------------------------------------------------------------------

  // Form streams.
  // Cleanup the IR before we try to form streams.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCSEPass());

  // Reorder blocks to increase the grouping of streamable ops.
  passManager.addNestedPass<mlir::FuncOp>(createHoistUnstreamableOpsPass());

  // The hoisting pass does some reordering. Canonicalize to avoid unnecessary
  // arbitrary ordering.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCSEPass());

  // Clone constants that escape basic blocks until we have better analysis.
  passManager.addNestedPass<mlir::FuncOp>(createInsertConstantClonesPass());

  // Group streamable ops into streams.
  passManager.addNestedPass<mlir::FuncOp>(createFormStreamsPass());

  // Prior to leaving the pipeline we need to clean things up for following
  // layers. These transforms may be undone by subsequent CSE/folding passes.
  passManager.addPass(createOutlineLargeConstantsPass());

  // Forming streams involves a fair amount of subgraph stitching, which can
  // cause duplication. Run CSE to collapse.
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCanonicalizerPass());
  passManager.addNestedPass<mlir::FuncOp>(mlir::createCSEPass());

  // Symbol DCE any remaining variables/functions that are now no longer
  // required.
  passManager.addPass(mlir::createSymbolDCEPass());
}

void registerFlowTransformPassPipeline() {
  PassPipelineRegistration<> transformPassPipeline(
      "iree-flow-transformation-pipeline",
      "Runs the full IREE flow dialect transformation pipeline",
      [](OpPassManager &passManager) {
        buildFlowTransformPassPipeline(passManager);
      });
}

namespace {
#define GEN_PASS_REGISTRATION
#include "iree/compiler/Dialect/Flow/Transforms/Passes.h.inc"  // IWYU pragma: export
}  // namespace

void registerFlowPasses() {
  // Generated.
  registerPasses();

  // Pipelines.
  registerFlowTransformPassPipeline();
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
