# Copyright 2020 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Macros for defining tests that run a module using iree-check-module."""

load("//iree/tools:compilation.bzl", "iree_bytecode_module")
load("//build_tools/bazel:run_binary_test.bzl", "run_binary_test")

ALL_TARGET_BACKENDS_AND_DRIVERS = [
    ("vmvx", "vmvx"),
    ("vulkan-spirv", "vulkan"),
    ("dylib-llvm-aot", "dylib"),
]

def iree_check_test(
        name,
        src,
        target_backend,
        driver,
        compiler_flags = [],
        runner_args = [],
        tags = [],
        timeout = None,
        **kwargs):
    """Creates an iree-check-module test for the specified source file.

    Args:
      name: name of the generated test.
      src: source mlir file containing the module.
      target_backend: target backend to compile for.
      driver: driver to run the module with.
      compiler_flags: additional flags to pass to the compiler. Bytecode translation and backend
          flags are passed automatically.
      runner_args: additional runner_args to pass to iree-check-module. The driver and input file
          are passed automatically.
      tags: additional tags to apply to the generated test. A tag "driver=DRIVER" is added
          automatically.
      timeout: timeout for the generated tests.
      **kwargs: any additional attributes to pass to the underlying run_binary_test.
    """
    bytecode_module_name = name + "_bytecode_module"
    iree_bytecode_module(
        name = bytecode_module_name,
        src = src,
        flags = [
            "-iree-mlir-to-vm-bytecode-module",
            "-mlir-print-op-on-diagnostic=false",
            "-iree-hal-target-backends=%s" % target_backend,
        ] + compiler_flags,
        visibility = ["//visibility:private"],
    )

    run_binary_test(
        name = name,
        args = [
            "--driver=%s" % driver,
            "$(location :%s)" % bytecode_module_name,
        ] + runner_args,
        data = [":%s" % bytecode_module_name],
        test_binary = "//iree/tools:iree-check-module",
        tags = tags + ["driver=%s" % driver],
        timeout = timeout,
        **kwargs
    )

def iree_check_single_backend_test_suite(
        name,
        srcs,
        target_backend,
        driver,
        compiler_flags = [],
        runner_args = [],
        tags = [],
        timeout = None,
        **kwargs):
    """Creates a test suite of iree-check-module tests for a single backend/driver pair.

    One test is generated per source file.

    Args:
      name: name of the generated test suite.
      srcs: source mlir files containing the module.
      target_backend: target backend to compile for.
      driver: driver to run the module with.
      compiler_flags: additional flags to pass to the compiler. Bytecode translation and backend
          flags are passed automatically.
      runner_args: additional runner_args to pass to the underlying iree-check-module tests. The
          driver and input file are passed automatically. To use different runner_args per test,
          create a separate suite or iree_check_test.
      tags: tags to apply to the generated tests. Note that as in standard test suites, manual
          is treated specially and will also apply to the test suite itself.
      timeout: timeout for the generated tests.
      **kwargs: any additional attributes to pass to the underlying tests and test suite.
    """
    tests = []
    for src in srcs:
        test_name = "_".join([name, src])
        iree_check_test(
            name = test_name,
            src = src,
            target_backend = target_backend,
            driver = driver,
            compiler_flags = compiler_flags,
            runner_args = runner_args,
            tags = tags,
            timeout = timeout,
            **kwargs
        )
        tests.append(test_name)
    native.test_suite(
        name = name,
        tests = tests,
        # Note that only the manual tag really has any effect here. Others are
        # used for test suite filtering, but all tests are passed the same tags.
        tags = tags,
        # If there are kwargs that need to be passed here which only apply to
        # the generated tests and not to test_suite, they should be extracted
        # into separate named arguments.
        **kwargs
    )

def iree_check_test_suite(
        name,
        srcs,
        target_backends_and_drivers = ALL_TARGET_BACKENDS_AND_DRIVERS,
        compiler_flags = [],
        runner_args = [],
        tags = [],
        **kwargs):
    """Creates a test suite of iree-check-module tests.

    One test is generated per source file and backend/driver.

    Args:
      name: name of the generated test suite.
      srcs: source mlir files containing the module.
      target_backends_and_drivers: backend/driver pairs to compile and run the module, respectively.
      compiler_flags: additional flags to pass to the compiler. Bytecode translation and backend
          flags are passed automatically.
      runner_args: additional runner_args to pass to the underlying iree-check-module tests. The
          driver and input file are passed automatically. To use different runner_args per test,
          create a separate suite or iree_check_test.
      tags: tags to apply to the generated tests. Note that as in standard test suites, manual
          is treated specially and will also apply to the test suite itself.
      **kwargs: any additional attributes to pass to the underlying tests and test suite.
    """

    # We could have complicated argument override logic for runner_args and such, or... the client
    # could just create a test suite. The latter seems simpler and more readable.
    tests = []
    for backend, driver in target_backends_and_drivers:
        suite_name = "_".join([name, backend, driver])
        iree_check_single_backend_test_suite(
            name = suite_name,
            srcs = srcs,
            driver = driver,
            target_backend = backend,
            compiler_flags = compiler_flags,
            runner_args = runner_args,
            tags = tags,
            **kwargs
        )
        tests.append(suite_name)
    native.test_suite(
        name = name,
        tests = tests,
        # Note that only the manual tag really has any effect here. Others are
        # used for test suite filtering, but all tests are passed the same tags.
        tags = tags,
        # If there are kwargs that need to be passed here which only apply to
        # the generated tests and not to test_suite, they should be extracted
        # into separate named arguments.
        **kwargs
    )
