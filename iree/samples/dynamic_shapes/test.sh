#!/bin/bash
# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# This script runs the steps laid out in the README for this sample. It is
# intended for use on continuous integration servers and as a reference for
# users, but can also be run manually.

set -x
set -e

ROOT_DIR=$(git rev-parse --show-toplevel)
BUILD_DIR=${ROOT_DIR}/build-samples
ARTIFACTS_DIR=/tmp/iree/colab_artifacts

# 1. Run the notebook to generate `counter.mlir` and `counter_vmvx.vmfb`
${ROOT_DIR}/build_tools/testing/run_python_notebook.sh \
  ${ROOT_DIR}/iree/samples/dynamic_shapes/dynamic_shapes.ipynb
test -f ${ARTIFACTS_DIR}/dynamic_shapes.mlir && echo "dynamic_shapes.mlir exists"

# 2. Compile the `iree-translate` tool.
cmake -B ${BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ${ROOT_DIR}
cmake --build ${BUILD_DIR} --target iree_tools_iree-translate

# 3. Compile `dynamic_shapes.mlir` using `iree-translate`.
${BUILD_DIR}/iree/tools/iree-translate \
  -iree-mlir-to-vm-bytecode-module \
  -iree-hal-target-backends=dylib-llvm-aot \
  -iree-input-type=mhlo \
  ${ARTIFACTS_DIR}/dynamic_shapes.mlir -o ${ARTIFACTS_DIR}/dynamic_shapes_dylib.vmfb

# 4. Build the `iree_samples_dynamic_shapes` CMake target.
cmake --build ${BUILD_DIR} --target iree_samples_dynamic_shapes

# 5. Run the sample binary.
${BUILD_DIR}/iree/samples/dynamic_shapes/dynamic-shapes \
  ${ARTIFACTS_DIR}/dynamic_shapes_dylib.vmfb dylib
