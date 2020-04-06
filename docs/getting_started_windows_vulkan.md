# Getting Started on Windows with Vulkan

[Vulkan](https://www.khronos.org/vulkan/) is a new generation graphics and
compute API that provides high-efficiency, cross-platform access to modern GPUs
used in a wide variety of devices from PCs and consoles to mobile phones and
embedded platforms.

IREE includes a Vulkan/[SPIR-V](https://www.khronos.org/registry/spir-v/) HAL
backend designed for executing advanced ML models in a deeply pipelined and
tightly integrated fashion on accelerators like GPUs.

This guide will walk you through using IREE's compiler and runtime Vulkan
components.

## Prerequisites

You should already have IREE cloned and building on your Windows machine. See
the [Getting Started on Windows with CMake](getting_started_windows_cmake.md) or
[Getting Started on Windows with Bazel](getting_started_windows_bazel.md) guide
for instructions.

You must have a physical GPU with drivers supporting Vulkan. We support using
[SwiftShader](https://swiftshader.googlesource.com/SwiftShader/) (a high
performance CPU-based implementation of Vulkan) on platforms where the
[Vulkan-ExtensionLayer](https://github.com/KhronosGroup/Vulkan-ExtensionLayer)
project builds, but it does not currently
[build on Windows](https://github.com/KhronosGroup/Vulkan-ExtensionLayer/issues/16).

Vulkan API version > 1.2 is recommended where available, though older versions
with support for the `VK_KHR_timeline_semaphore` extension may also work.

## Vulkan Setup

### Background

Vulkan applications interface with Vulkan "drivers", "layers", and "extensions"
through the Vulkan loader. See LunarG's
[Architecture of the Vulkan Loader Interfaces](https://vulkan.lunarg.com/doc/view/latest/windows/loader_and_layer_interface.html)
page for more information.

### Quick Start

The
[dynamic_symbols_test](https://github.com/google/iree/blob/master/iree/hal/vulkan/dynamic_symbols_test.cc)
checks if the Vulkan loader and a valid ICD are accessible.

Run the test:

```shell
# -- CMake --
$ cmake --build build\ --target iree_hal_vulkan_dynamic_symbols_test
$ .\build\iree\hal\vulkan\iree_hal_vulkan_dynamic_symbols_test.exe

# -- Bazel --
$ bazel test iree/hal/vulkan:dynamic_symbols_test
```

Tests in IREE's HAL "Conformence Test Suite" (CTS) actually exercise the Vulkan
HAL, which includes checking for supported layers and extensions.

Run the
[allocator test](https://github.com/google/iree/blob/master/iree/hal/cts/allocator_test.cc):

```shell
# -- CMake --
$ cmake --build build\ --target iree_hal_cts_allocator_test
$ .\build\iree\hal\cts\iree_hal_cts_allocator_test.exe

# -- Bazel --
$ bazel test iree/hal/cts:allocator_test
```

If these tests pass, you can skip down to the next section.

### Setting up the Vulkan Loader

If you see failures to find `vulkan-1.dll` (the Vulkan loader), install it by
either:

*   Updating your system's GPU drivers
*   Installing the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)
*   Building the Vulkan loader
    [from source](https://github.com/KhronosGroup/Vulkan-Loader)

<!--
### Setting up SwiftShader

TODO(scotttodd): Document when SwiftShader supports `VK_KHR_timeline_semaphore`
                 Or Vulkan-ExtensionLayer builds for Windows

### Setting up Vulkan-ExtensionLayer

TODO(scotttodd): Document when Vulkan-ExtensionLayer builds for Windows
-->

## Using IREE's Vulkan Compiler Target and Runtime Driver

### Compiling for the Vulkan HAL

Pass the flag `-iree-hal-target-backends=vulkan-spirv` to `iree-translate.exe`:

```shell
# -- CMake --
$ cmake --build build\ --target iree_tools_iree-translate
$ .\build\iree\tools\iree-translate.exe -iree-mlir-to-vm-bytecode-module -iree-hal-target-backends=vulkan-spirv .\iree\tools\test\simple.mlir -o .\build\module.fb

# -- Bazel --
$ bazel run iree/tools:iree-translate -- -iree-mlir-to-vm-bytecode-module -iree-hal-target-backends=vulkan-spirv .\iree\tools\test\simple.mlir -o .\build\module.fb
```

> Tip:<br>
> &nbsp;&nbsp;&nbsp;&nbsp;If successful, this may have no output. You can pass
> other flags like `-print-ir-after-all` to control the program.

### Executing modules with the Vulkan driver

Pass the flag `-driver=vulkan` to `iree-run-module.exe`:

```shell
# -- CMake --
$ cmake --build build\ --target iree_tools_iree-run-module
$ .\build\iree\tools\iree-run-module.exe -input_file=.\build\module.fb -driver=vulkan -entry_function=abs -inputs="i32=-2"

# -- Bazel --
$ bazel run iree/tools:iree-run-module -- -input_file=.\build\module.fb -driver=vulkan -entry_function=abs -inputs="i32=-2"
```

## Running IREE's Vulkan Samples

Install the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/), then run:

```shell
# -- CMake --
$ cmake --build build\ --target iree_samples_vulkan_vulkan_inference_gui
$ .\build\iree\samples\vulkan\vulkan_inference_gui.exe

# -- Bazel --
$ bazel run iree/samples/vulkan:vulkan_inference_gui
```

## Troubleshooting

If loading Vulkan fails, try running one the test or programs again with
`VK_LOADER_DEBUG=all` set:

```shell
# -- CMake --
$ set VK_LOADER_DEBUG=all
$ .\build\iree\hal\vulkan\iree_hal_vulkan_dynamic_symbols_test.exe

# -- Bazel --
$ bazel test iree/hal/vulkan:dynamic_symbols_test --test_env=VK_LOADER_DEBUG=all
```

## What's next?

More documentation coming soon...

<!-- TODO(scotttodd): link to Vulkan debugging, developer guides -->
