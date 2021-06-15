// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_DIALECT_HAL_TARGET_VULKANSPIRV_VULKANSPIRVTARGET_H_
#define IREE_COMPILER_DIALECT_HAL_TARGET_VULKANSPIRV_VULKANSPIRVTARGET_H_

#include <functional>
#include <string>

#include "iree/compiler/Conversion/Passes.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

// Options controlling the SPIR-V translation.
struct VulkanSPIRVTargetOptions {
  // SPIR-V codegeneration options
  SPIRVCodegenOptions codegenOptions;
  // Vulkan target environment as #vk.target_env attribute assembly.
  std::string vulkanTargetEnv;
  // Vulkan target triple.
  std::string vulkanTargetTriple;
};

// Returns a VulkanSPIRVTargetOptions struct initialized with Vulkan/SPIR-V
// related command-line flags.
VulkanSPIRVTargetOptions getVulkanSPIRVTargetOptionsFromFlags();

// Registers the Vulkan/SPIR-V backends.
void registerVulkanSPIRVTargetBackends(
    std::function<VulkanSPIRVTargetOptions()> queryOptions);

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_HAL_TARGET_VULKANSPIRV_VULKANSPIRVTARGET_H_
