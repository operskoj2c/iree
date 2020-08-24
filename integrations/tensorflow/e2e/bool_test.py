# Lint as: python3
# Copyright 2020 Google LLC
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
"""Tests for ops in the tf.math module."""

import numpy as np
from pyiree.tf.support import tf_test_utils
import tensorflow.compat.v2 as tf


class MathModule(tf.Module):

  @tf.function(input_signature=[tf.TensorSpec([4], tf.float32)])
  def greater_than(self, x):
    return x > 1.0

  @tf.function(input_signature=[
      tf.TensorSpec([4], tf.bool),
      tf.TensorSpec([4], tf.bool)
  ])
  def logical_and(self, x, y):
    return tf.math.logical_and(x, y)


@tf_test_utils.compile_module(MathModule)
class BooleanTest(tf_test_utils.TracedModuleTestCase):

  def test_greater_than(self):

    def greater_than(module):
      module.greater_than(np.array([0.0, 1.2, 1.5, 3.75], dtype=np.float32))

    self.compare_backends(greater_than)

  def test_logical_and(self):

    def logical_and(module):
      module.logical_and(
          np.array([True, True, False, False], dtype=np.bool),
          np.array([True, False, False, True], dtype=np.bool))

    self.compare_backends(logical_and)


if __name__ == "__main__":
  if hasattr(tf, "enable_v2_behavior"):
    tf.enable_v2_behavior()
  tf.test.main()
