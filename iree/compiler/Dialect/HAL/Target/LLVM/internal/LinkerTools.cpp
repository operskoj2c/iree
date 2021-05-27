// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Target/LLVM/LinkerTool.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

// TODO(benvanik): add other platforms:
// createMacLinkerTool using ld64.lld

std::unique_ptr<LinkerTool> createAndroidLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);
std::unique_ptr<LinkerTool> createEmbeddedLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);
std::unique_ptr<LinkerTool> createRiscvLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);
std::unique_ptr<LinkerTool> createUnixLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);
std::unique_ptr<LinkerTool> createWasmLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);
std::unique_ptr<LinkerTool> createWindowsLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions);

// static
std::unique_ptr<LinkerTool> LinkerTool::getForTarget(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions) {
  if (targetOptions.linkEmbedded) {
    return createEmbeddedLinkerTool(targetTriple, targetOptions);
  } else if (targetTriple.isAndroid()) {
    return createAndroidLinkerTool(targetTriple, targetOptions);
  } else if (targetTriple.isOSWindows() ||
             targetTriple.isWindowsMSVCEnvironment()) {
    return createWindowsLinkerTool(targetTriple, targetOptions);
  } else if (targetTriple.isWasm()) {
    return createWasmLinkerTool(targetTriple, targetOptions);
  } else if (targetTriple.isRISCV()) {
    return createRiscvLinkerTool(targetTriple, targetOptions);
  }
  return createUnixLinkerTool(targetTriple, targetOptions);
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
