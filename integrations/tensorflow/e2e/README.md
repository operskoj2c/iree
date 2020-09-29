# TensorFlow e2e tests

This is a collection of e2e tests that compile a TensorFlow model with IREE (and
potentially TFLite), run it on multiple backends, and crosscheck the results.

## Pre-Requisites

You will need a TensorFlow 2.0+ nightly installed in your python environment:
the python binary in `$PYTHON_BIN` should be able to `import tensorflow` and
that TensorFlow should be version 2.0+. This can be checked with
`tensorflow.version`.

See [Install TensorFlow with pip](https://www.tensorflow.org/install/pip) for
instructions.

## Vulkan Setup

If you do not have your environment setup to use IREE with Vulkan (see
[this doc](https://google.github.io/iree/get-started/generic-vulkan-env-setup)),
then you can run the manual test targets with
`--target_backends=tf,iree_vmla,iree_llvmjit` (that is, by omitting
`iree_vulkan` from the list of backends to run the tests on).

The test suites can be run excluding Vulkan by specifying
`--test_tag_filters="-driver=vulkan"` in the `bazel test` invocation, or by
adding `test --test_tag_filters="-driver=vulkan"` to your `user.bazelrc`.

## Compiling `tf.Module`s

Compatible TensorFlow modules can be compiled to specific IREE backends using
`IreeCompiledModule`. This also optionally saves compilation artifacts to a
specified directory. These artifacts include MLIR across various lowerings and
the compiled VM FlatBuffer. A basic example of creating and calling an
`IreeCompiledModule` can be found in
[`tf_utils_test.py`](https://github.com/google/iree/blob/main/integrations/tensorflow/bindings/python/pyiree/tf/support/tf_utils_test.py)

When using Keras models or tf.Modules with functions that IREE can't compile,
`exported_names` should be specified. For example:

```python
from pyiree.tf.support import tf_utils
vmla_module = tf_utils.IreeCompiledModule(
    module_class=KerasTFModuleClass,
    backend_info=tf_utils.BackendInfo('iree_vmla'),
    exported_names=['predict'])
vmla_module.predict(...)
```

## Running Tests

For locally running tests and iterating on backend development, `bazel run` is
preferred.

```shell
# Run math_test on all backends.
bazel run //integrations/tensorflow/e2e:math_test_manual

# Run math_test comparing TensorFlow to itself (e.g. to debug randomization).
bazel run //integrations/tensorflow/e2e:math_test_manual -- --target_backends=tf

# Run math_test comparing the VMLA backend and TensorFlow.
bazel run //integrations/tensorflow/e2e:math_test_manual -- --target_backends=iree_vmla

# Run math_test comparing the VMLA backend to itself multiple times.
bazel run //integrations/tensorflow/e2e:math_test_manual -- \
  --reference_backend=iree_vmla --target_backends=iree_vmla,iree_vmla
```

For reproducibility of the unit tests `CompiledModule()` sets the random seeds
of `tf`, `numpy` and `python` by calling `tf_utils.set_random_seed()` before
model creation.

## Writing Tests

Our tests use a class `TracedModule` to capture and store all of the inputs and
outputs of a `CompiledModule` in a `Trace`. Each unittest on a `TestCase` uses
the `compare_backends` method. This method runs the function it is passed with a
`TracedModule` once for each reference and target backend. The inputs and
outputs to these modules are then checked for correctness, using the reference
backend as a source of truth. For example:

```python
# Compile a `tf.Module` named `SimpleArithmeticModule` into a `CompiledModule`.
@tf_test_utils.compile_module(SimpleArithmeticModule)
# Inherit from `TracedModuleTestCase`.
class SimpleArithmeticTest(tf_test_utils.TracedModuleTestCase):

  # Unit test.
  def test_simple_mul(self):

    # Trace function.
    def simple_mul(module):
      # A random seed is automatically set before each call to `simple_mul`.
      a = tf_utils.uniform([4])
      b = np.array([400., 5., 6., 7.], dtype=np.float32)

      # The inputs `a` and `b` are recorded along with the output `c`
      c = module.simple_mul(a, b)

      # The inputs `a` and `b` are recorded along with the (unnamed) output
      # module.simple_mul returns.
      module.simple_mul(a, c)

    # Calls `simple_mul` once for each backend, recording the inputs and outputs
    # to `module` and then comparing them.
    self.compare_backends(simple_mul)
```

## Test Suites

Test targets are automatically generated for each test file and for each backend
to check numerical correctness against TensorFlow. Tests targets that pass are
placed into the `e2e_tests` test suite. Tests that fail on particular backends
are recorded in lists in the `BUILD` files. For example, if
`experimental_new_test.py` fails on the `iree_llvmjit` and `iree_vulkan`
backends then the following lines should be added to the `BUILD` file:

```build
LLVM_FAILING = [
    ...
    "experimental_new_test.py",
    ...
]

VULKAN_FAILING = [
    ...
    "experimental_new_test.py",
    ...
]
```

Test targets for these backends are placed into the `e2e_tests_failing` test
suite. Test targets in these test suites can be run as follows:

```shell
# Run all e2e tests that are expected to pass.
bazel test //integrations/tensorflow/e2e:e2e_tests

# Run all e2e tests that are expected to fail.
bazel test //integrations/tensorflow/e2e:e2e_tests_failing

# Run a specific failing e2e test target.
# Note that generated test targets are prefixed with their test suite name.
# Also, if broadcasting_test starts working on iree_vulkan after the time
# of writing then this command will fail.
bazel test //integrations/tensorflow/e2e:e2e_tests_failing_broadcasting_test__tf__iree_vulkan
```

## Generated Artifacts

By default, running an E2E test generates a number of compilation, debugging and
benchmarking artifacts in `/tmp/iree/modules/`. The location of these artifacts
can be changed via the `--artifacts_dir` flag. The generated directory structure
for each module is as follows:

```shell
/tmp/iree/modules/ModuleName
  ├── tf_input.mlir
  │   # MLIR for ModuleName in TF's input dialect.
  ├── iree_input.mlir
  │   # tf_input.mlir translated to IREE MLIR.
  ├── iree_vmla
  │   # Or any other IREE backend.
  │   ├── compiled.vmfb
  │   │   # A flatbuffer containing IREE's compiled code.
  │   └── traces
  │       # Directory with a trace for each unittest in vision_model_test.py.
  │       ├── trace_function_1
  │       │   # Directory storing logs and serialization for a specific trace.
  │       │   │── flagfile
  │       │   │   # An Abseil flagfile containing arguments
  │       │   │   # iree-benchmark-module needs to benchmark this trace.
  │       │   └── log.txt
  │       │       # A more detailed version of the test logs.
  │       │── trace_function_2
  │       └── ...
  ├── tflite  # If TFLite supports compiling ModuleName.
  │   ├── method_1.tflite  # Methods on ModuleName compiled to bytes with TFLite
  │   │   # A method on ModuleName compiled to bytes with TFLite, which can
  │   │   # be ingested by TFLite's benchmark_model binary.
  │   ├── method_2.tflite
  │   └── traces
  │       └── ...
  └── tf_ref  # Directory storing the tensorflow reference traces.
      └── traces
          └── ...
```

Traces for a particular test can be loaded via the `Trace.load(trace_dir)`
method. For example:

```python
ref_trace = Trace.load("/tmp/iree/modules/ModuleName/tf_ref/traces/predict/")
tar_trace = Trace.load("/tmp/iree/modules/ModuleName/iree_vmla/traces/predict/")
abs_diff = np.abs(ref_trace.calls[0].outputs[0] - tar_trace.calls[0].outputs[0])
print(np.mean(abs_diff))
```

Traces are named after the trace functions defined in their unittests. So in the
`SimpleArithmeticModule` example above, the `trace_dir` would be
`/tmp/iree/modules/SimpleArithmeticModule/iree_vmla/traces/simple_mul/`.

## Benchmarking E2E Modules

We use our end-to-end TensorFlow integrations tests to generate tested
compilation and benchmarking artifacts. This allows us to validate that our
benchmarks are behaving as we expect them to, and to run them using valid inputs
for each model. An overview of how to run benchmarks on IREE and TFLite can be
found in [this doc](TODO(meadowlark)).

## Debugging Tests

If the compiler fails to compile the program, then it will create a crash
reproducer (see [MLIR documentation](https://mlir.llvm.org/docs/WritingAPass/)),
which then allows reproducing the bug with an appropriate "opt" tool. Further
debugging iteration can happen in opt.

TODO(silvasean): debugging miscompiles

## Legacy TFLite Compilation

_Please don't use this unless you are forced to._

We support using `tf.compat.v1.lite.TFLiteConverter.from_saved_model` to compile
older `tf.Module`s with TFLite. This will be used if the `tf.Module` being
tested has a method named `get_legacy_tflite_saved_model_converter_kwargs`. This
method must return a dict with the following kwargs: `model_path`,
`input_arrays`, `output_arrays`, and `exported_name`. The module must use only
one exported name, and `exported_name` should be equal to that name. See
`mobile_bert_squad_test.py` for a concrete example.
