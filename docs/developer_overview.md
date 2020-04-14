# Developer Overview

This guide provides an overview of IREE's project structure and main tools for
developers.

## Project Code Layout

[iree/](https://github.com/google/iree/blob/master/iree/)

*   Core IREE project

[integrations/](https://github.com/google/iree/blob/master/integrations/)

*   Integrations between IREE and other frameworks, such as TensorFlow

[bindings/](https://github.com/google/iree/blob/master/bindings/)

*   Language and platform bindings, such as Python

[colab/](https://github.com/google/iree/blob/master/colab/)

*   Colab notebooks for interactively using IREE's Python bindings

## IREE Code Layout

[iree/base/](https://github.com/google/iree/blob/master/iree/base/)

*   Common types and utilities used throughout IREE

[iree/compiler/](https://github.com/google/iree/blob/master/iree/compiler/)

*   IREE's MLIR dialects, LLVM compiler passes, module translation code, etc.

[iree/hal/](https://github.com/google/iree/blob/master/iree/hal/)

*   **H**ardware **A**bstraction **L**ayer for IREE's runtime, with
    implementations for hardware and software backends

[iree/schemas/](https://github.com/google/iree/blob/master/iree/schemas/)

*   Shared data storage format definitions, primarily using
    [FlatBuffers](https://google.github.io/flatbuffers/)

[iree/tools/](https://github.com/google/iree/blob/master/iree/tools/)

*   Assorted tools used to optimize, translate, and evaluate IREE

[iree/vm/](https://github.com/google/iree/blob/master/iree/vm/)

*   Bytecode **V**irtual **M**achine used to work with IREE modules and invoke
    IREE functions

## Developer Tools

IREE's compiler components accept programs and code fragments in several
formats, including high level TensorFlow Python code, serialized TensorFlow
[SavedModel](https://www.tensorflow.org/guide/saved_model) programs, and lower
level textual MLIR files using combinations of supported dialects like `xla_hlo`
and IREE's internal dialects. While input programs are ultimately compiled down
to modules suitable for running on some combination of IREE's target deployment
platforms, IREE's developer tools can run individual compiler passes,
translations, and other transformations step by step.

### iree-opt

`iree-opt` is a tool for testing IREE's compiler passes. It is similar to
[mlir-opt](https://github.com/llvm/llvm-project/tree/master/mlir/tools/mlir-opt)
and runs sets of IREE's compiler passes on `.mlir` input files. See "conversion"
in [MLIR's Glossary](https://mlir.llvm.org/getting_started/Glossary/#conversion)
for more information.

Test `.mlir` files that are checked in typically include a `RUN` block at the
top of the file that specifies which passes should be performed and if
`FileCheck` should be used to test the generated output.

For example, to run some passes on the
[reshape.mlir](https://github.com/google/iree/blob/master/iree/compiler/Translation/SPIRV/XLAToSPIRV/test/reshape.mlir)
test file:

```shell
$ bazel run //iree/tools:iree-opt -- \
  -split-input-file \
  -iree-index-computation \
  -simplify-spirv-affine-exprs=false \
  -convert-iree-to-spirv \
  -verify-diagnostics \
  $PWD/iree/compiler/Translation/SPIRV/XLAToSPIRV/test/reshape.mlir
```

Custom passes may also be layered on top of `iree-opt`, see
[iree/samples/custom_modules/dialect](https://github.com/google/iree/blob/master/iree/samples/custom_modules/dialect)
for a sample.

### iree-translate

`iree-translate` converts MLIR input into external formats like IREE modules. It
is similar to
[mlir-translate](https://github.com/llvm/llvm-project/tree/master/mlir/tools/mlir-translate),
see "translation" in
[MLIR's Glossary](https://mlir.llvm.org/getting_started/Glossary/#translation)
for more information.

For example, to translate `simple.mlir` to an IREE module:

```shell
$ bazel run //iree/tools:iree-translate -- \
  -iree-mlir-to-vm-bytecode-module \
  --iree-hal-target-backends=vmla \
  $PWD/iree/tools/test/simple.mlir \
  -o /tmp/module.fb
```

Custom translations may also be layered on top of `iree-translate`, see
[iree/samples/custom_modules/dialect](https://github.com/google/iree/blob/master/iree/samples/custom_modules/dialect)
for a sample.

### iree-run-module

The `iree-run-module` program takes an already translated IREE module as input
and executes an exported main function using the provided inputs.

This program can be used in sequence with `iree-translate` to translate a
`.mlir` file to an IREE module and then execute it. Here is an example command
that executes the simple `module.fb` compiled from `simple.mlir` above on IREE's
VMLA driver:

```shell
$ bazel run //iree/tools:iree-run-module -- \
  --input_file=/tmp/module.fb \
  --driver=vmla \
  --entry_function=abs \
  --inputs="i32=-2"
```

### iree-run-mlir

The `iree-run-mlir` program takes a `.mlir` file as input, translates it to an
IREE bytecode module, and executes the module.

It is designed for testing and debugging, not production uses, and therefore
does some additional work that usually must be explicit, like marking every
function as exported by default and running all of them.

For example, to execute the contents of
[iree/tools/test/simple.mlir](https://github.com/google/iree/blob/master/iree/tools/test/simple.mlir):

```shell
$ bazel run //iree/tools:iree-run-mlir -- \
  $PWD/iree/tools/test/simple.mlir \
  --input-value="i32=-2" \
  --iree-hal-target-backends=vmla
```

### iree-dump-module

The `iree-dump-module` program prints the contents of an IREE module FlatBuffer
file.

For example, to inspect the module translated above:

```shell
$ bazel run //iree/tools:iree-dump-module -- /tmp/module.fb
```
