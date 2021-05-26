// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_DIALECT_HAL_TARGET_CUDA_CUDATARGET_H_
#define IREE_COMPILER_DIALECT_HAL_TARGET_CUDA_CUDATARGET_H_

#include "iree/compiler/Dialect/HAL/Target/TargetBackend.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

// Options controlling the CUDA translation.
struct CUDATargetOptions {};

// Returns a CUDATargetOptions struct initialized with the
// --iree-hal-cuda-* flags.
CUDATargetOptions getCUDATargetOptionsFromFlags();

// Registers the CUDA backends.
void registerCUDATargetBackends(
    std::function<CUDATargetOptions()> queryOptions);

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_HAL_TARGET_CUDA_CUDATARGET_H_
