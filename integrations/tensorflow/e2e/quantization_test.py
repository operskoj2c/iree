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

from absl import app
import numpy as np
from pyiree.tf.support import tf_test_utils
from pyiree.tf.support import tf_utils
import tensorflow.compat.v2 as tf


class QuantizationModule(tf_test_utils.TestModule):

  @tf_test_utils.tf_function_unit_test(
      input_signature=[tf.TensorSpec([32], tf.float32)],
      input_generator=lambda *args: tf_utils.uniform(*args, low=-6, high=6))
  def fake_quant(self, x):
    return tf.quantization.fake_quant_with_min_max_args(x,
                                                        min=-6,
                                                        max=6,
                                                        num_bits=8,
                                                        narrow_range=False,
                                                        name=None)


class QuantizationTest(tf_test_utils.TracedModuleTestCase):

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._modules = tf_test_utils.compile_tf_module(QuantizationModule)


def main(argv):
  del argv  # Unused
  if hasattr(tf, 'enable_v2_behavior'):
    tf.enable_v2_behavior()

  QuantizationTest.generate_unit_tests(QuantizationModule)
  tf.test.main()


if __name__ == '__main__':
  app.run(main)
