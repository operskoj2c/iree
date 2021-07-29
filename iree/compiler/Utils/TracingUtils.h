// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_UTILS_TRACINGUTILS_H_
#define IREE_COMPILER_UTILS_TRACINGUTILS_H_

#include "iree/base/tracing.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassInstrumentation.h"

namespace mlir {
namespace iree_compiler {

// Instruments passes using IREE's runtime tracing support.
//
// Usage:
//   passManager.addInstrumentation(std::make_unique<PassTracing>());
struct PassTracing : public PassInstrumentation {
  PassTracing() {}
  ~PassTracing() override = default;

#if IREE_ENABLE_COMPILER_TRACING && \
    IREE_TRACING_FEATURES & IREE_TRACING_FEATURE_INSTRUMENTATION
  void runBeforePass(Pass *pass, Operation *op) override;
  void runAfterPass(Pass *pass, Operation *op) override;
  void runAfterPassFailed(Pass *pass, Operation *op) override;
#endif  // IREE_TRACING_FEATURES & IREE_TRACING_FEATURE_INSTRUMENTATION
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_UTILS_TRACINGUTILS_H_
