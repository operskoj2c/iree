// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Target/SPIRVCommon/SPIRVTarget.h"

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "mlir/Dialect/SPIRV/IR/TargetAndABI.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

SPIRVTargetBackend::SPIRVTargetBackend(SPIRVCodegenOptions options)
    : spvCodeGenOptions_(std::move(options)) {}

void SPIRVTargetBackend::declareTargetOpsForEnv(
    IREE::Flow::ExecutableOp sourceOp, IREE::HAL::ExecutableOp executableOp,
    spirv::TargetEnvAttr spvTargetEnv) {
  auto targetBuilder = OpBuilder::atBlockTerminator(&executableOp.getBlock());
  auto targetOp = targetBuilder.create<IREE::HAL::ExecutableTargetOp>(
      sourceOp.getLoc(), name(), filter_pattern());

  auto containerBuilder = OpBuilder::atBlockTerminator(&targetOp.getBlock());
  auto innerModuleOp = containerBuilder.create<ModuleOp>(sourceOp.getLoc());

  // Attach SPIR-V target environment to the target's ModuleOp.
  // If we had multiple target environments we would generate one target op
  // per environment, with each setting its own environment attribute.
  innerModuleOp->setAttr(spirv::getTargetEnvAttrName(), spvTargetEnv);
}

void SPIRVTargetBackend::buildTranslationPassPipeline(
    OpPassManager &passManager) {
  buildSPIRVTransformPassPipeline(passManager, spvCodeGenOptions_);
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
