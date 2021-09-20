# Lint as: python3
# Copyright 2020 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import contextlib
import logging
import os
import io
import tempfile
import unittest

import iree.compiler

SIMPLE_MUL_ASM = """
func @simple_mul(%arg0: tensor<4xf32>, %arg1: tensor<4xf32>) -> tensor<4xf32> {
    %0 = "mhlo.multiply"(%arg0, %arg1) {name = "mul.1"} : (tensor<4xf32>, tensor<4xf32>) -> tensor<4xf32>
    return %0 : tensor<4xf32>
}
"""


class CompilerTest(unittest.TestCase):

  def setUp(self):
    if "IREE_SAVE_TEMPS" in os.environ:
      del os.environ["IREE_SAVE_TEMPS"]

  def testNoTargetBackends(self):
    with self.assertRaisesRegex(
        ValueError, "Expected a non-empty list for 'target_backends'"):
      binary = iree.compiler.compile_str(SIMPLE_MUL_ASM)

  def testCompileStr(self):
    binary = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type="mhlo",
        target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)
    logging.info("Flatbuffer size = %d", len(binary))
    self.assertTrue(binary)

  # Compiling the string form means that the compiler does not have a valid
  # source file name, which can cause issues on the AOT side. Verify
  # specifically. See: https://github.com/google/iree/issues/4439
  def testCompileStrLLVMAOT(self):
    binary = iree.compiler.compile_str(SIMPLE_MUL_ASM,
                                       input_type="mhlo",
                                       target_backends=["dylib-llvm-aot"])
    logging.info("Flatbuffer size = %d", len(binary))
    self.assertTrue(binary)

  # Verifies that multiple target_backends are accepted. Which two are not
  # load bearing.
  # See: https://github.com/google/iree/issues/4436
  def testCompileMultipleBackends(self):
    binary = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type="mhlo",
        target_backends=["dylib-llvm-aot", "vulkan-spirv"])
    logging.info("Flatbuffer size = %d", len(binary))
    self.assertTrue(binary)

  def testCompileInputFile(self):
    with tempfile.NamedTemporaryFile("wt", delete=False) as f:
      try:
        f.write(SIMPLE_MUL_ASM)
        f.close()
        binary = iree.compiler.compile_file(
            f.name,
            input_type="mhlo",
            target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)
      finally:
        os.remove(f.name)
    logging.info("Flatbuffer size = %d", len(binary))
    self.assertIn(b"simple_mul", binary)

  def testCompileOutputFile(self):
    with tempfile.NamedTemporaryFile("wt", delete=False) as f:
      try:
        f.close()
        output = iree.compiler.compile_str(
            SIMPLE_MUL_ASM,
            input_type="mhlo",
            output_file=f.name,
            target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)
        self.assertIsNone(output)

        with open(f.name, "rb") as f_read:
          binary = f_read.read()
      finally:
        os.remove(f.name)
    logging.info("Flatbuffer size = %d", len(binary))
    self.assertIn(b"simple_mul", binary)

  def testOutputFbText(self):
    text = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type="mhlo",
        output_format=iree.compiler.OutputFormat.FLATBUFFER_TEXT,
        target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS).decode("utf-8")
    # Just check for an arbitrary JSON-tag.
    self.assertIn('"exported_functions"', text)

  def testBadInputType(self):
    with self.assertRaisesRegex(
        ValueError, "For input_type= argument, expected one of: "
        "NONE, MHLO, TOSA"):
      _ = iree.compiler.compile_str(
          SIMPLE_MUL_ASM,
          input_type="not-existing",
          output_format="foobar",
          target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)

  def testBadOutputFormat(self):
    with self.assertRaisesRegex(
        ValueError, "For output_format= argument, expected one of: "
        "FLATBUFFER_BINARY, FLATBUFFER_TEXT, MLIR_TEXT"):
      _ = iree.compiler.compile_str(
          SIMPLE_MUL_ASM,
          input_type="mhlo",
          output_format="foobar",
          target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)

  def testOutputFbTextParsed(self):
    text = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type='mhlo',
        output_format='flatbuffer_text',
        target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS).decode("utf-8")
    # Just check for an arbitrary JSON-tag.
    self.assertIn('"exported_functions"', text)

  def testOutputMlirText(self):
    text = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type="mhlo",
        output_format=iree.compiler.OutputFormat.MLIR_TEXT,
        target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS).decode("utf-8")
    # Just check for a textual op name.
    self.assertIn("vm.module", text)

  def testExtraArgsStderr(self):
    # mlir-timing is not special: it just does something and emits to stderr.
    with io.StringIO() as buf, contextlib.redirect_stderr(buf):
      iree.compiler.compile_str(
          SIMPLE_MUL_ASM,
          input_type="mhlo",
          extra_args=["--mlir-timing"],
          target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)
      stderr = buf.getvalue()
    self.assertIn("Execution time report", stderr)

  def testAllOptions(self):
    binary = iree.compiler.compile_str(
        SIMPLE_MUL_ASM,
        input_type="mhlo",
        optimize=False,
        strip_debug_ops=True,
        strip_source_map=True,
        crash_reproducer_path="foobar.txt",
        # Re-enable when benchmarking pass is fixed: #6196
        # enable_benchmark=True,
        target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)

  def testException(self):
    with self.assertRaisesRegex(iree.compiler.CompilerToolError,
                                "Invoked with"):
      _ = iree.compiler.compile_str(
          "I'm a little teapot but not a valid program",
          target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)

  def testExplicitTempFileSaver(self):
    temp_dir = tempfile.TemporaryDirectory()
    output_file = tempfile.NamedTemporaryFile("wt")
    output_file.close()
    with iree.compiler.TempFileSaver(temp_dir.name):
      output = iree.compiler.compile_str(
          SIMPLE_MUL_ASM,
          input_type="mhlo",
          output_file=output_file.name,
          target_backends=iree.compiler.DEFAULT_TESTING_BACKENDS)
      self.assertIsNone(output)

    # There should be an output file and a core-output.bin in the temp dir.
    self.assertTrue(os.path.exists(output_file.name))
    expected_temp_file = os.path.join(temp_dir.name, "core-output.bin")
    self.assertTrue(os.path.exists(expected_temp_file))

    # And they should have the same contents.
    with open(output_file.name, "rb") as f:
      output_contents = f.read()
    with open(expected_temp_file, "rb") as f:
      temp_contents = f.read()
    self.assertEqual(temp_contents, output_contents)
    temp_dir.cleanup()

  def testEnvTempFileSaver(self):
    temp_dir = tempfile.TemporaryDirectory()
    os.environ["IREE_SAVE_TEMPS"] = temp_dir.name
    with iree.compiler.TempFileSaver() as tfs:
      self.assertTrue(tfs.retained)
      self.assertEqual(tfs.retained_path, temp_dir.name)

  def testTempFileSaverDisabled(self):
    with iree.compiler.TempFileSaver() as tfs:
      self.assertFalse(tfs.retained)


if __name__ == "__main__":
  logging.basicConfig(level=logging.DEBUG)
  unittest.main()
