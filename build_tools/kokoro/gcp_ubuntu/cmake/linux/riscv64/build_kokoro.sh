#!/bin/bash

# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Cross-compile the project towards riscv with the
# gcr.io/iree-oss/riscv image using Kokoro.
# Requires the environment variables KOKORO_ROOT and KOKORO_ARTIFACTS_DIR, which
# are set by Kokoro.

set -x
set -e
set -o pipefail

# Print the UTC time when set -x is on
export PS4='[$(date -u "+%T %Z")] '

source "${KOKORO_ARTIFACTS_DIR?}/github/iree/build_tools/kokoro/gcp_ubuntu/docker_common.sh"

# Sets DOCKER_RUN_ARGS
docker_setup

docker run "${DOCKER_RUN_ARGS[@]?}" \
  gcr.io/iree-oss/riscv@sha256:24a401f9d47a16a0b460bf34a8485f087b7d88944ed661dd1201ce2d6c19df3e \
  build_tools/kokoro/gcp_ubuntu/cmake/linux/riscv64/build.sh

# Kokoro will rsync this entire directory back to the executor orchestrating the
# build which takes forever and is totally useless.
rm -rf "${KOKORO_ARTIFACTS_DIR?}"/*

# Print out artifacts dir contents after deleting them as a coherence check.
ls -1a "${KOKORO_ARTIFACTS_DIR?}/"
