// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// IREE source.mlir -> execution output test runner.
// This is meant to be called from LIT for FileCheck tests, and tries to match
// the interface of mlir-opt (featuring -split-input-file, etc) so it's easier
// to work with there. If you want a more generalized runner for standalone
// precompiled IREE modules use //third_party/iree/tools:run_module.
//
// By default all exported functions in the module will be run in order.
// All input values, provided via -input-values, will be passed to the
// functions (this means all input signatures must match). Results from the
// executed functions will be printed to stdout for checking.
// Use -output_types to set the function output data types, which like args will
// be used for all functions executed.
//
// Example input:
// // RUN: iree-run %s | FileCheck %s
// // CHECK-LABEL: @foo
// // CHECK: 1xf32: 2
// func @foo() -> memref<f32> attributes {iree.module.export} {
//   %0 = "iree.constant"() {value: dense<tensor<f32>, 2.0>} : () -> memref<f32>
//   return %0 : memref<f32>
// }

#include "absl/flags/flag.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "iree/base/init.h"
#include "iree/base/source_location.h"
#include "iree/base/status.h"
#include "iree/compiler/Translation/SequencerModuleTranslation.h"
#include "iree/hal/buffer_view_string_util.h"
#include "iree/hal/driver_registry.h"
#include "iree/schemas/module_def_generated.h"
#include "iree/vm/bytecode_tables_sequencer.h"
#include "iree/vm/debug/debug_server_flags.h"
#include "iree/vm/fiber_state.h"
#include "iree/vm/instance.h"
#include "iree/vm/module.h"
#include "iree/vm/module_printer.h"
#include "iree/vm/sequencer_context.h"
#include "third_party/llvm/llvm/include/llvm/ADT/StringRef.h"
#include "third_party/llvm/llvm/include/llvm/Support/SourceMgr.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Attributes.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Function.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/MLIRContext.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/IR/Module.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Parser.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Support/FileUtilities.h"

ABSL_FLAG(bool, split_input_file, true,
          "Split the input file into multiple modules.");

ABSL_FLAG(std::string, target_backends, "",
          "Comma-separated list of target backends to translate executables "
          "into. Omit to translate using all linked-in backend translators.");
ABSL_FLAG(
    bool, export_all, true,
    "Automatically add the iree.module.export attribute to all functions.");

ABSL_FLAG(std::string, input_values, "", "Input shapes and optional values.");
ABSL_FLAG(std::string, output_types, "",
          "Output data types (comma delimited list of b/i/u/f for "
          "binary/signed int/unsigned int/float).");

// TODO(benvanik): is there a more canonical flag we can use?
ABSL_FLAG(bool, print_mlir, true, "Prints MLIR IR during translation.");

ABSL_FLAG(bool, print_bytecode, false,
          "Prints IREE bytecode after translation.");

namespace iree {
namespace {

using ::iree::hal::BufferView;
using ::iree::vm::Function;
using ::iree::vm::Module;
using ::iree::vm::ModuleFile;

// Returns a driver name capable of handling input from the given backend.
std::string BackendToDriverName(std::string backend) {
  size_t dash = backend.find('-');
  if (dash == std::string::npos) {
    return backend;
  } else {
    return backend.substr(0, dash);
  }
}

// Prepares a module for evaluation by running MLIR import and IREE translation.
StatusOr<std::unique_ptr<Module>> PrepareModule(
    std::string target_backend,
    std::unique_ptr<llvm::MemoryBuffer> file_buffer) {
  mlir::MLIRContext context;

  // Parse input MLIR module.
  llvm::SourceMgr source_mgr;
  source_mgr.AddNewSourceBuffer(std::move(file_buffer), llvm::SMLoc());
  mlir::OwningModuleRef mlir_module =
      mlir::parseSourceFile(source_mgr, &context);

  if (absl::GetFlag(FLAGS_export_all)) {
    for (auto function : mlir_module->getOps<mlir::FuncOp>()) {
      function.setAttr("iree.module.export", mlir::UnitAttr::get(&context));
    }
  }

  // Translate from MLIR to IREE bytecode.
  mlir::iree_compiler::ModuleTranslationOptions options;
  options.print_mlir = absl::GetFlag(FLAGS_print_mlir);
  options.target_backends = {target_backend};
  auto iree_module_bytes =
      mlir::iree_compiler::translateMlirToIreeSequencerModule(mlir_module.get(),
                                                              options);
  if (iree_module_bytes.empty()) {
    return iree::InternalErrorBuilder(IREE_LOC)
           << "Error translating MLIR to an IREE sequencer module";
  }

  if (absl::GetFlag(FLAGS_print_mlir)) {
    mlir_module->dump();
  }

  // Wrap module in a file handle.
  ASSIGN_OR_RETURN(auto iree_module_file,
                   ModuleFile::FromBuffer(ModuleDefIdentifier(),
                                          std::move(iree_module_bytes)));
  return Module::FromFile(std::move(iree_module_file));
}

// Parses a list of input shapes and values from a string of newline-separated
// inputs. Expects the contents to have one value per line with each value
// listed as
//   [shape]xtype=[value]
// Example:
//   4x4xi8=0,1,2,3
StatusOr<std::vector<BufferView>> ParseInputsFromFlags(
    hal::Allocator* allocator) {
  std::string file_contents =
      absl::StrReplaceAll(absl::GetFlag(FLAGS_input_values), {{"\\n", "\n"}});
  std::vector<BufferView> inputs;
  for (const auto& line :
       absl::StrSplit(file_contents, '\n', absl::SkipWhitespace())) {
    ASSIGN_OR_RETURN(auto input,
                     hal::ParseBufferViewFromString(line, allocator));
    inputs.push_back(input);
  }
  return inputs;
}

// Outputs all results from the function to stdout in IREE BufferView format.
Status OutputFunctionResults(const Function& function,
                             absl::Span<BufferView> results) {
  std::vector<std::string> output_types =
      absl::StrSplit(absl::GetFlag(FLAGS_output_types),
                     absl::delimiter::AnyOf(", "), absl::SkipWhitespace());
  if (!output_types.empty() && output_types.size() != results.size()) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "--output_types= specified but has " << output_types.size()
           << " types when the function returns " << results.size();
  }

  for (int i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    auto print_mode = hal::BufferViewPrintMode::kFloatingPoint;
    if (!output_types.empty()) {
      ASSIGN_OR_RETURN(print_mode,
                       hal::ParseBufferViewPrintMode(output_types[i]));
    }
    ASSIGN_OR_RETURN(auto result_str,
                     hal::PrintBufferViewToString(result, print_mode, 1024));
    LOG(INFO) << "result[" << i << "]: " << result.buffer->DebugString();
    std::cout << result_str << "\n";
  }

  return OkStatus();
}

// Evaluates a single function in its own fiber, printing the results to stdout.
Status EvaluateFunction(std::shared_ptr<vm::Instance> instance,
                        vm::SequencerContext* context,
                        hal::Allocator* allocator, const Function& function) {
  // Setup our dummy fiber we will run with.
  vm::FiberState fiber_state(instance);

  std::cout << "EXEC @" << function.name() << std::endl;

  // Marshal inputs.
  ASSIGN_OR_RETURN(std::vector<BufferView> args,
                   ParseInputsFromFlags(allocator));
  std::vector<BufferView> results;
  results.resize(function.result_count());

  // Call into the main function.
  RETURN_IF_ERROR(context->Invoke(&fiber_state, function, absl::MakeSpan(args),
                                  absl::MakeSpan(results)));

  // Print outputs.
  RETURN_IF_ERROR(OutputFunctionResults(function, absl::MakeSpan(results)));

  return OkStatus();
}

// Evaluates all exported functions within given module.
Status EvaluateFunctions(absl::string_view target_backend,
                         std::unique_ptr<Module> module) {
  // Create the context we'll use for this (ensuring that we can't interfere
  // with other running evaluations, such as when in a multithreaded test
  // runner).
  ASSIGN_OR_RETURN(auto debug_server, vm::debug::CreateDebugServerFromFlags());
  auto instance = std::make_shared<vm::Instance>(std::move(debug_server));
  ASSIGN_OR_RETURN(auto driver, hal::DriverRegistry::shared_registry()->Create(
                                    target_backend));
  ASSIGN_OR_RETURN(auto device, driver->CreateDefaultDevice());
  RETURN_IF_ERROR(instance->device_manager()->RegisterDevice(device));
  vm::SequencerContext context(instance);

  if (absl::GetFlag(FLAGS_print_bytecode)) {
    vm::PrintModuleFlagBitfield print_flags = vm::PrintModuleFlag::kNone;
    RETURN_IF_ERROR(vm::PrintModuleToStream(vm::sequencer_opcode_table(),
                                            *module, print_flags, &std::cout));
  }

  // Register module with the context.
  RETURN_IF_ERROR(context.RegisterModule(std::move(module)));

  // Evaluate all exported functions.
  for (auto& module : context.modules()) {
    for (int function_ordinal : *module->def().function_table()->exports()) {
      ASSIGN_OR_RETURN(auto function, module->function_table().LookupFunction(
                                          function_ordinal));
      RETURN_IF_ERROR(
          EvaluateFunction(instance, &context, device->allocator(), function));
    }
  }

  RETURN_IF_ERROR(instance->device_manager()->UnregisterDevice(device.get()));
  device.reset();
  driver.reset();

  return OkStatus();
}

// Translates and runs a single LLVM file buffer.
Status EvaluateFile(std::unique_ptr<llvm::MemoryBuffer> file_buffer) {
  std::vector<std::string> target_backends;
  if (absl::GetFlag(FLAGS_target_backends).empty()) {
    target_backends =
        hal::DriverRegistry::shared_registry()->EnumerateAvailableDrivers();
  } else {
    // We need to map specific backends names to drivers (like 'vulkan-spirv' to
    // the driver 'vulkan').
    target_backends = absl::StrSplit(absl::GetFlag(FLAGS_target_backends), ',');
  }

  for (auto target_backend : target_backends) {
    // Prepare the module for execution and evaluate it.
    auto cloned_file_buffer = llvm::MemoryBuffer::getMemBufferCopy(
        file_buffer->getBuffer(), file_buffer->getBufferIdentifier());
    ASSIGN_OR_RETURN(auto module, PrepareModule(target_backend + '*',
                                                std::move(cloned_file_buffer)));
    RETURN_IF_ERROR(EvaluateFunctions(BackendToDriverName(target_backend),
                                      std::move(module)));
  }

  return OkStatus();
}

// Runs the given .mlir file based on the current flags.
Status RunFile(std::string mlir_filename) {
  // Load input file/from stdin.
  std::string error_message;
  auto file = mlir::openInputFile(mlir_filename, &error_message);
  if (!file) {
    return NotFoundErrorBuilder(IREE_LOC)
           << "Unable to open input file " << mlir_filename << ": "
           << error_message;
  }

  if (!absl::GetFlag(FLAGS_split_input_file)) {
    // Use entire buffer as a single module.
    return EvaluateFile(std::move(file));
  }

  // Split the buffer into separate modules and evaluate independently.
  // This matches the -split-input-file arg to mlir-opt.
  const char kSplitMarker[] = "// -----\n";
  auto* full_buffer = file.get();
  llvm::SmallVector<llvm::StringRef, 8> source_buffers;
  full_buffer->getBuffer().split(source_buffers, kSplitMarker);

  // Add the original buffer to the source manager.
  llvm::SourceMgr fileSourceMgr;
  fileSourceMgr.AddNewSourceBuffer(std::move(file), llvm::SMLoc());

  // Process each chunk in turn. Only return the first error (but log all).
  Status any_failure;
  for (auto& sub_source_buffer : source_buffers) {
    auto split_loc = llvm::SMLoc::getFromPointer(sub_source_buffer.data());
    unsigned split_line = fileSourceMgr.getLineAndColumn(split_loc).first;
    auto sub_buffer = llvm::MemoryBuffer::getMemBufferCopy(
        sub_source_buffer, full_buffer->getBufferIdentifier() +
                               llvm::Twine(" split at line #") +
                               llvm::Twine(split_line));
    auto sub_failure = EvaluateFile(std::move(sub_buffer));
    if (!sub_failure.ok()) {
      LOG(ERROR) << sub_failure;
      if (any_failure.ok()) {
        any_failure = std::move(sub_failure);
      }
    }
  }

  return any_failure;
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  InitializeEnvironment(&argc, &argv);
  if (argc < 2) {
    LOG(ERROR) << "Must supply an input .mlir file.";
    return 1;
  }
  QCHECK_OK(RunFile(argv[1]));
  return 0;
}

}  // namespace iree
