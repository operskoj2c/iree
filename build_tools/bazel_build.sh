#!/bin/bash

# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build the IREE project with bazel. Designed for CI, but can be run manually.

set -e

set -x

# CI-friendly defaults that control availability of certain platform tests.
if ! [[ -v IREE_VULKAN_DISABLE ]]; then
  IREE_VULKAN_DISABLE=1
fi
test_env_args=(
  --test_env=IREE_VULKAN_DISABLE=$IREE_VULKAN_DISABLE
)
echo "Running with test env args: ${test_env_args[@]}"

# Build and test everything not explicitly marked as excluded from CI (using the
# tag "notap", "Test Automation Platform").
# Note that somewhat contrary to its name `bazel test` will also build
# any non-test targets specified.
# We use `bazel query //...` piped to `bazel test` rather than the simpler
# `bazel test //...` because the latter excludes targets tagged "manual". The
# "manual" tag allows targets to be excluded from human wildcard builds, but we
# want them built by CI unless they are excluded with "notap".
bazel query '//... except attr("tags", "notap", //...) except attr("tags", "nokokoro", //...) except //bindings/... except //integrations/... except //iree/hal/vulkan:dynamic_symbols_test except //iree/samples/rt:bytecode_module_api_test except //iree/samples/simple_embedding:simple_embedding_test' | \
    xargs bazel test ${test_env_args[@]} --config=rbe --config=rs --keep_going --test_output=errors
