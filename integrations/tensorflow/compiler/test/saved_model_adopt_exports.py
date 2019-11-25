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
"""Tests supported features of saved models."""

# pylint: disable=invalid-name
# pylint: disable=missing-docstring
# pylint: disable=line-too-long

import pyiree
import tensorflow.compat.v2 as tf

SAVED_MODEL_IMPORT_PASSES = [
    "tf-executor-graph-pruning",
    "tf-standard-pipeline",
    "iree-tf-saved-model-adopt-exports",
    "canonicalize",
]


# Tests that a simple example with flat args and a single result and no
# captures imports properly.
# CHECK-LABEL: RUN_TEST: T0001_FlatArgsResultsNoBoundGlobals
# CHECK: module
# CHECK-NOT: tf_saved_model.semantics
# CHECK: @simple_mul_no_capture
# CHECK: iree.module.export
# CHECK: FINISH_TEST
class T0001_FlatArgsResultsNoBoundGlobals(tf.Module):

  @tf.function(input_signature=[
      tf.TensorSpec([4], tf.float32),
      tf.TensorSpec([4], tf.float32)
  ])
  def simple_mul_no_capture(self, a, b):
    return a * b


pyiree.tf_test_driver.add_test(
    test_name="T0001_FlatArgsResultsNoBoundGlobals",
    tf_module_builder=T0001_FlatArgsResultsNoBoundGlobals,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True)

# T0002: Tests that bound global vars import properly.


# CHECK-LABEL: RUN_TEST: T0002a_SimpleVarRead
# CHECK: flow.variable @v mutable dense<0.000000e+00> : tensor<f32>
# CHECK: func @f() -> (tensor<f32> {tf_saved_model.index_path = []})
# CHECK: attributes{{.*}}iree.module.export
# CHECK:   flow.variable.load @v : tensor<f32>
# CHECK: FINISH_TEST
class T0002a_SimpleVarRead(tf.Module):

  def __init__(self):
    self.v = tf.Variable(0.)

  @tf.function(input_signature=[])
  def f(self):
    return self.v


# CHECK-LABEL: RUN_TEST: T0002b_SimpleVarWrite
# CHECK: flow.variable @v mutable dense<0.000000e+00> : tensor<f32>
# CHECK: func @f(%arg0: tensor<f32> {tf_saved_model.index_path = [0]})
# CHECK: attributes{{.*}}iree.module.export
# CHECK:   flow.variable.store @v, %arg0 : tensor<f32>
# CHECK: FINISH_TEST
class T0002b_SimpleVarWrite(tf.Module):

  def __init__(self):
    self.v = tf.Variable(0.)

  @tf.function(input_signature=[tf.TensorSpec([], tf.float32)])
  def f(self, a):
    self.v.assign(a)


# CHECK-LABEL: RUN_TEST: T0002c_SimpleConst
#  flow.variable [[CONST:@.+]] dense<0.000000e+00> : tensor<f32>
#  func @f() -> (tensor<f32> {tf_saved_model.index_path = []})
#  attributes{{.*}}iree.module.export
#    flow.variable.load [[CONST]] : tensor<f32>
# CHECK: FINISH_TEST
class T0002c_SimpleConst(tf.Module):

  def __init__(self):
    self.c = tf.constant(0.)

  @tf.function(input_signature=[])
  def f(self):
    return self.c


# CHECK-LABEL: RUN_TEST: T0002d_VarCompatibleShapeChange
# CHECK: flow.variable @v mutable dense<0.000000e+00> : tensor<1xf32>
# CHECK: func @f()
# CHECK: attributes{{.*}}iree.module.export
# CHECK:   [[CONST_2xf32:%.+]] = "tf.Const"() {value = dense<[0.000000e+00, 1.000000e+00]> : tensor<2xf32>} : () -> tensor<2xf32>
# CHECK:   [[CONST_3xf32:%.+]] = "tf.Const"() {value = dense<[0.000000e+00, 1.000000e+00, 2.000000e+00]> : tensor<3xf32>} : () -> tensor<3xf32>
# CHECK:   flow.variable.store @v, [[CONST_2xf32]] : tensor<2xf32>
# CHECK:   flow.variable.store @v, [[CONST_3xf32]] : tensor<3xf32>
# CHECK: FINISH_TEST
class T0002d_VarCompatibleShapeChange(tf.Module):

  def __init__(self):
    self.v = tf.Variable([0.], shape=[None])

  @tf.function(input_signature=[])
  def f(self):
    self.v.assign(tf.constant([0., 1.]))
    self.v.assign(tf.constant([0., 1., 2.]))


# CHECK-LABEL: RUN_TEST: T0002e_Error_VarMultipleExportedNames
# CHECK: [ERROR]: Multiple exported names for global tensor not supported yet
# CHECK: FINISH_TEST
class T0002e_Error_VarMultipleExportedNames(tf.Module):

  def __init__(self):
    self.v = tf.Variable(0.)
    self.v2 = self.v


# CHECK-LABEL: RUN_TEST: T0002f_Error_UnsupportedResourceOp
# CHECK: [ERROR]: unknown op operating on resource for global tensor
# CHECK: FINISH_TEST
class T0002f_Error_UnsupportedResourceOp(tf.Module):

  def __init__(self):
    self.v = tf.Variable([0.], shape=[None])

  @tf.function(input_signature=[])
  def f(self):
    self.v.assign_add(tf.constant([0., 1.]))


pyiree.tf_test_driver.add_test(
    test_name="T0002a_SimpleVarRead",
    tf_module_builder=T0002a_SimpleVarRead,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True)
pyiree.tf_test_driver.add_test(
    test_name="T0002b_SimpleVarWrite",
    tf_module_builder=T0002b_SimpleVarWrite,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True)
pyiree.tf_test_driver.add_test(
    test_name="T0002c_SimpleConst",
    tf_module_builder=T0002c_SimpleConst,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True)
pyiree.tf_test_driver.add_test(
    test_name="T0002d_VarCompatibleShapeChange",
    tf_module_builder=T0002d_VarCompatibleShapeChange,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True)
pyiree.tf_test_driver.add_test(
    test_name="T0002e_Error_VarMultipleExportedNames",
    tf_module_builder=T0002e_Error_VarMultipleExportedNames,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)
pyiree.tf_test_driver.add_test(
    test_name="T0002f_Error_UnsupportedResourceOp",
    tf_module_builder=T0002f_Error_UnsupportedResourceOp,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)


# Tests that a structured argument is handled properly.
# NOTE: This is currently an error and needs to be implemented
# CHECK-LABEL: RUN_TEST: T0003_StructuredArgs
# CHECK: [ERROR]: This pass doesn't support structured arguments yet
# CHECK: FINISH_TEST_WITH_EXCEPTION
class T0003_StructuredArgs(tf.Module):

  @tf.function(input_signature=[{
      "x": tf.TensorSpec([4], tf.float32),
      "y": tf.TensorSpec([4], tf.float32)
  }])
  def simple_mul(self, d):
    return d["x"] * d["y"]


pyiree.tf_test_driver.add_test(
    test_name="T0003_StructuredArgs",
    tf_module_builder=T0003_StructuredArgs,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)


# Tests that a structured argument is handled properly.
# NOTE: This is currently an error and needs to be implemented
# CHECK-LABEL: RUN_TEST: T0003_StructuredMultipleResult
# CHECK: [ERROR]: This pass doesn't support multiple results yet
# CHECK: FINISH_TEST_WITH_EXCEPTION
class T0003_StructuredMultipleResult(tf.Module):

  @tf.function(input_signature=[
      tf.TensorSpec([4], tf.float32),
      tf.TensorSpec([4], tf.float32)
  ])
  def simple_mul(self, a, b):
    product = a * b
    return {"x": product, "x_squared": product * product}


pyiree.tf_test_driver.add_test(
    test_name="T0003_StructuredMultipleResult",
    tf_module_builder=T0003_StructuredMultipleResult,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)


# Tests that a structured argument is handled properly.
# NOTE: This is currently an error and needs to be implemented
# CHECK-LABEL: RUN_TEST: T0004_StructuredSingleResult
# CHECK: [ERROR]: This pass doesn't support structured results yet
# CHECK: FINISH_TEST_WITH_EXCEPTION
class T0004_StructuredSingleResult(tf.Module):

  @tf.function(input_signature=[
      tf.TensorSpec([4], tf.float32),
      tf.TensorSpec([4], tf.float32)
  ])
  def simple_mul(self, a, b):
    product = a * b
    return {"x": product}


pyiree.tf_test_driver.add_test(
    test_name="T0004_StructuredSingleResult",
    tf_module_builder=T0004_StructuredSingleResult,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)


# Tests that a structured argument is handled properly.
# NOTE: This is currently an error and needs to be implemented
# CHECK-LABEL: RUN_TEST: T0005_MultipleExportedFuncNames
# CHECK: [ERROR]: Multiple exported names not supported yet
# CHECK: FINISH_TEST_WITH_EXCEPTION
class T0005_MultipleExportedFuncNames(tf.Module):

  @tf.function(input_signature=[
      tf.TensorSpec([4], tf.float32),
      tf.TensorSpec([4], tf.float32)
  ])
  def simple_mul(self, a, b):
    product = a * b
    return {"x": product}


# Force a function alias.
T0005_MultipleExportedFuncNames.another_copy = (
    T0005_MultipleExportedFuncNames.simple_mul)

pyiree.tf_test_driver.add_test(
    test_name="T0005_MultipleExportedFuncNames",
    tf_module_builder=T0005_MultipleExportedFuncNames,
    passes=SAVED_MODEL_IMPORT_PASSES,
    print_input_module=True,
    expect_pass_failure=True)

if __name__ == "__main__":
  pyiree.tf_test_driver.run_tests(__file__, with_filecheck=True)
