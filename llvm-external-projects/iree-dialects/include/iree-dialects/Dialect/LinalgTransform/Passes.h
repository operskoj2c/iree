// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <memory>

namespace mlir {
namespace linalg {
namespace transform {

void registerLinalgTransformInterpreterPass();
void registerLinalgTransformExpertExpansionPass();
void registerDropSchedulePass();

} // namespace transform
} // namespace linalg
} // namespace mlir

namespace mlir {
class Pass;
std::unique_ptr<Pass> createLinalgTransformInterpreterPass();
std::unique_ptr<Pass> createDropSchedulePass();
} // namespace mlir
