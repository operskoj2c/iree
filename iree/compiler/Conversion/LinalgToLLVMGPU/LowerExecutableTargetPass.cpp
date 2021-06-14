// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Conversion/CodegenUtils/FunctionUtils.h"
#include "iree/compiler/Conversion/Common/Passes.h"
#include "iree/compiler/Conversion/LinalgToLLVMGPU/KernelConfig.h"
#include "iree/compiler/Conversion/LinalgToLLVMGPU/Passes.h"
#include "iree/compiler/Dialect/HAL/IR/HALDialect.h"
#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"

namespace mlir {
namespace iree_compiler {

namespace {
/// Lowers an hal.executable.target operation to scalar/native-vector
/// code. Invokes different compilation pipeline to
/// - first lower to scalar/native-vector code
/// - then convert to NVVM/ROCDL dialect.
/// This should be merged with the equivalent pass in LinalgToLLVM. Fo
/// simplicity it is currently a separate pass.
class LowerExecutableTargetPass
    : public PassWrapper<LowerExecutableTargetPass,
                         OperationPass<IREE::HAL::ExecutableTargetOp>> {
 public:
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<IREE::HAL::HALDialect, linalg::LinalgDialect,
                    vector::VectorDialect, gpu::GPUDialect>();
  }

  LowerExecutableTargetPass() = default;
  LowerExecutableTargetPass(const LowerExecutableTargetPass &pass) = default;

  void runOnOperation() override;
};
}  // namespace

void LowerExecutableTargetPass::runOnOperation() {
  IREE::HAL::ExecutableTargetOp targetOp = getOperation();
  ModuleOp moduleOp = targetOp.getInnerModule();

  OpPassManager executableLoweringPipeline(
      IREE::HAL::ExecutableTargetOp::getOperationName());

  if (failed(initGPULaunchConfig(moduleOp))) {
    return signalPassFailure();
  }
  // There might be multiple entry points in the module. Currently, all of
  // them need to have the same pipeline.
  // TODO(ravishankarm): This is strange that this is not enforced
  // structurally, but something to address later on. For now this restriction
  // is fine.
  llvm::StringMap<IREE::HAL::ExecutableEntryPointOp> entryPoints =
      getAllEntryPoints(moduleOp);
  Optional<IREE::HAL::DispatchLoweringPassPipeline> passPipeline;
  SmallVector<int64_t, 4> workloadPerWorkgroup;
  for (auto &it : entryPoints) {
    auto entryPointOp = it.second;
    if (IREE::HAL::TranslationInfo translationInfo =
            getTranslationInfo(entryPointOp)) {
      IREE::HAL::DispatchLoweringPassPipeline currPipeline =
          translationInfo.passPipeline().getValue();
      if (ArrayAttr workloadPerWorkgroupAttr =
              translationInfo.workloadPerWorkgroup()) {
        workloadPerWorkgroup = llvm::to_vector<4>(llvm::map_range(
            workloadPerWorkgroupAttr,
            [](Attribute attr) { return attr.cast<IntegerAttr>().getInt(); }));
      }
      if (passPipeline) {
        if (currPipeline != passPipeline.getValue()) {
          moduleOp.emitError(
              "unhandled compilation of entry point function with different "
              "pass pipelines within a module");
          return signalPassFailure();
        }
        continue;
      }
      passPipeline = currPipeline;
    }
  }
  auto funcOps = moduleOp.getOps<FuncOp>();
  FuncOp funcOp = *funcOps.begin();
  // Attach the workgroup size as an attribute. This will be used when
  // creating the flatbuffer.
  funcOp->setAttr(
      "llvmgpu_workgroup_size",
      DenseElementsAttr::get<int64_t>(
          VectorType::get(3, IntegerType::get(moduleOp.getContext(), 64)),
          workloadPerWorkgroup));

  switch (*passPipeline) {
    case IREE::HAL::DispatchLoweringPassPipeline::LLVMGPUDistribute:
      addGPUSimpleDistributePassPipeline(executableLoweringPipeline);
      break;
    case IREE::HAL::DispatchLoweringPassPipeline::LLVMGPUVectorize:
      addGPUVectorizationPassPipeline(executableLoweringPipeline);
      break;
    default:
      llvm_unreachable("Unsupported pipeline on GPU target.");
  }

  if (failed(runPipeline(executableLoweringPipeline, targetOp))) {
    return signalPassFailure();
  }
}

std::unique_ptr<OperationPass<IREE::HAL::ExecutableTargetOp>>
createLowerExecutableTargetGPUPass() {
  return std::make_unique<LowerExecutableTargetPass>();
}

static PassRegistration<LowerExecutableTargetPass> pass(
    "iree-lower-executable-target-gpu-pass",
    "Perform lowering of executable target using one of the "
    "IREE::HAL::DispatchLoweringPassPipeline",
    [] { return std::make_unique<LowerExecutableTargetPass>(); });

}  // namespace iree_compiler
}  // namespace mlir
