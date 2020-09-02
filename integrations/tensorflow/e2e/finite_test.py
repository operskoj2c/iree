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

import numpy as np
from pyiree.tf.support import tf_test_utils
import tensorflow.compat.v2 as tf


class FiniteModule(tf.Module):

  @tf.function(input_signature=[tf.TensorSpec([4], tf.float32)])
  def finite(self, x):
    return tf.math.is_finite(x)


@tf_test_utils.compile_module(FiniteModule)
class FiniteTest(tf_test_utils.TracedModuleTestCase):

  def test_finite(self):

    def finite(module):
      module.finite(np.array([0.0, 1.2, -5.0, np.inf], dtype=np.float32))

    self.compare_backends(finite)


if __name__ == "__main__":
  if hasattr(tf, "enable_v2_behavior"):
    tf.enable_v2_behavior()
  tf.test.main()
