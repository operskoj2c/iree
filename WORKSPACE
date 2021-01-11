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

# Workspace file for the IREE project.
# buildozer: disable=positional-args

workspace(name = "iree_core")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

###############################################################################
# bazel toolchains rules for remote execution (https://releases.bazel.build/bazel-toolchains.html).
http_archive(
    name = "bazel_toolchains",
    sha256 = "8c9728dc1bb3e8356b344088dfd10038984be74e1c8d6e92dbb05f21cabbb8e4",
    strip_prefix = "bazel-toolchains-3.7.1",
    urls = [
        "https://github.com/bazelbuild/bazel-toolchains/releases/download/3.7.1/bazel-toolchains-3.7.1.tar.gz",
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/3.7.1/bazel-toolchains-3.7.1.tar.gz",
    ],
)

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

rbe_autoconfig(
    name = "rbe_default",
    base_container_digest = "sha256:1a8ed713f40267bb51fe17de012fa631a20c52df818ccb317aaed2ee068dfc61",
    digest = "sha256:d69c260b98a97ad430d34c4591fb2399e00888750f5d47ede00c1e6f3e774e5a",
    registry = "gcr.io",
    repository = "iree-oss/rbe-toolchain",
    use_checked_in_confs = "Force",
)

###############################################################################

###############################################################################
# io_bazel_rules_closure
# This is copied from https://github.com/tensorflow/tensorflow/blob/v2.0.0-alpha0/WORKSPACE.
# Dependency of:
#   TensorFlow (boilerplate for tf_workspace(), apparently)
http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "5b00383d08dd71f28503736db0500b6fb4dda47489ff5fc6bed42557c07c6ba9",
    strip_prefix = "rules_closure-308b05b2419edb5c8ee0471b67a40403df940149",
    urls = [
        "https://storage.googleapis.com/mirror.tensorflow.org/github.com/bazelbuild/rules_closure/archive/308b05b2419edb5c8ee0471b67a40403df940149.tar.gz",
        "https://github.com/bazelbuild/rules_closure/archive/308b05b2419edb5c8ee0471b67a40403df940149.tar.gz",  # 2019-06-13
    ],
)
###############################################################################

###############################################################################
# Skylib
# Dependency of:
#   TensorFlow
http_archive(
    name = "bazel_skylib",
    sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
    ],
)

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")

bazel_skylib_workspace()
###############################################################################

###############################################################################
# llvm-project

maybe(
    local_repository,
    name = "llvm_bazel",
    path = "third_party/llvm-bazel/llvm-bazel",
)

load("@llvm_bazel//:zlib.bzl", "llvm_zlib_disable")

maybe(
    llvm_zlib_disable,
    name = "llvm_zlib",
)

load("@llvm_bazel//:terminfo.bzl", "llvm_terminfo_disable")

maybe(
    llvm_terminfo_disable,
    name = "llvm_terminfo",
)

load("@llvm_bazel//:configure.bzl", "llvm_configure")

maybe(
    llvm_configure,
    name = "llvm-project",
    src_path = "third_party/llvm-project",
    src_workspace = "@iree_core//:WORKSPACE",
)
###############################################################################

###############################################################################
# Bootstrap TensorFlow.
# Note that we ultimately would like to avoid doing this at the top level like
# this but need to unbundle some of the deps from the tensorflow repo first.
# In the mean-time: we're sorry.
# TODO(laurenzo): Come up with a way to make this optional. Also, see if we can
# get the TensorFlow tf_repositories() rule to use maybe() so we can provide
# local overrides safely.
maybe(
    local_repository,
    name = "org_tensorflow",
    path = "third_party/tensorflow",
)

# TF depends on tf_toolchains.
http_archive(
    name = "tf_toolchains",
    sha256 = "d60f9637c64829e92dac3f4477a2c45cdddb9946c5da0dd46db97765eb9de08e",
    strip_prefix = "toolchains-1.1.5",
    urls = [
        "http://mirror.tensorflow.org/github.com/tensorflow/toolchains/archive/v1.1.5.tar.gz",
        "https://github.com/tensorflow/toolchains/archive/v1.1.5.tar.gz",
    ],
)

# Import all of the tensorflow dependencies.
load("@org_tensorflow//tensorflow:workspace.bzl", "tf_repositories")
###############################################################################

###############################################################################
# Find and configure the Vulkan SDK, if installed.
load("//build_tools/third_party/vulkan_sdk:repo.bzl", "vulkan_sdk_setup")

maybe(
    vulkan_sdk_setup,
    name = "vulkan_sdk",
)
###############################################################################

maybe(
    local_repository,
    name = "com_google_absl",
    path = "third_party/abseil-cpp",
)

maybe(
    local_repository,
    name = "com_google_ruy",
    path = "third_party/ruy",
)

maybe(
    local_repository,
    name = "com_google_googletest",
    path = "third_party/googletest",
)

maybe(
    new_local_repository,
    name = "com_github_dvidelabs_flatcc",
    build_file = "build_tools/third_party/flatcc/BUILD.overlay",
    path = "third_party/flatcc",
)

# TODO(scotttodd): TensorFlow is squatting on the vulkan_headers repo name, so
# we use a temporary one until resolved. Theirs is set to an outdated version.
maybe(
    new_local_repository,
    name = "iree_vulkan_headers",
    build_file = "build_tools/third_party/vulkan_headers/BUILD.overlay",
    path = "third_party/vulkan_headers",
)

maybe(
    new_local_repository,
    name = "vulkan_memory_allocator",
    build_file = "build_tools/third_party/vulkan_memory_allocator/BUILD.overlay",
    path = "third_party/vulkan_memory_allocator",
)

maybe(
    local_repository,
    name = "spirv_headers",
    path = "third_party/spirv_headers",
)

maybe(
    local_repository,
    name = "com_google_benchmark",
    path = "third_party/benchmark",
)

maybe(
    new_local_repository,
    name = "renderdoc_api",
    build_file = "build_tools/third_party/renderdoc_api/BUILD.overlay",
    path = "third_party/renderdoc_api",
)

maybe(
    new_local_repository,
    name = "com_github_pytorch_cpuinfo",
    build_file = "build_tools/third_party/cpuinfo/BUILD.overlay",
    path = "third_party/cpuinfo",
)

maybe(
    new_local_repository,
    name = "pffft",
    build_file = "build_tools/third_party/pffft/BUILD.overlay",
    path = "third_party/pffft",
)

maybe(
    new_local_repository,
    name = "half",
    build_file = "build_tools/third_party/half/BUILD.overlay",
    path = "third_party/half",
)

maybe(
    new_local_repository,
    name = "spirv_cross",
    build_file = "build_tools/third_party/spirv_cross/BUILD.overlay",
    path = "third_party/spirv_cross",
)

# Bootstrap TensorFlow deps last so that ours can take precedence.
tf_repositories()
