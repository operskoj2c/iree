// Copyright 2020 Google LLC
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

#include "iree/compiler/Dialect/VM/Target/C/CModuleTarget.h"

#include "emitc/Target/Cpp.h"
#include "iree/compiler/Dialect/IREE/IR/IREEOps.h"
#include "iree/compiler/Dialect/IREE/Transforms/Passes.h"
#include "iree/compiler/Dialect/VM/Conversion/VMToEmitC/ConvertVMToEmitC.h"
#include "iree/compiler/Dialect/VM/Target/CallingConventionUtils.h"
#include "iree/compiler/Dialect/VM/Transforms/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace VM {

static std::string buildFunctionName(IREE::VM::ModuleOp &moduleOp,
                                     IREE::VM::FuncOp &funcOp,
                                     bool implSuffix) {
  std::string functionName =
      std::string(moduleOp.getName()) + "_" + std::string(funcOp.getName());

  return implSuffix ? functionName + "_impl" : functionName;
}

static void printModuleComment(IREE::VM::ModuleOp &moduleOp,
                               llvm::raw_ostream &output) {
  output << "//" << std::string(77, '=') << "\n"
         << "// module \"" << moduleOp.getName()
         << "\"\n"
            "//"
         << std::string(77, '=') << "\n";
}

static void printSeparatingComment(llvm::raw_ostream &output) {
  output << "//" << std::string(77, '=')
         << "\n"
            "// The code below setups functions and lookup tables to "
            "implement the vm\n"
            "// interface\n"
            "//"
         << std::string(77, '=') << "\n";
}

static LogicalResult printStructDefinitions(IREE::VM::ModuleOp &moduleOp,
                                            mlir::emitc::CppEmitter &emitter) {
  llvm::raw_ostream &output = emitter.ostream();
  std::string moduleName = moduleOp.getName().str();

  output << "struct " << moduleName << "_s;\n";
  output << "struct " << moduleName << "_state_s {\n";

  output << "iree_allocator_t allocator;\n";
  output << "uint8_t rwdata["
         << moduleOp.ordinal_counts().getValue().global_bytes() << "];\n";
  output << "iree_vm_ref_t refs["
         << moduleOp.ordinal_counts().getValue().global_refs() << "];\n";
  output << "};\n";

  output << "typedef struct " << moduleName << "_s " << moduleName << "_t;\n";
  output << "typedef struct " << moduleName << "_state_s " << moduleName
         << "_state_t;\n";

  output << "\n";

  return success();
}

static LogicalResult printShim(IREE::VM::FuncOp &funcOp,
                               llvm::raw_ostream &output) {
  auto callingConvention = makeCallingConventionString(funcOp);
  if (!callingConvention) {
    return funcOp.emitError("Couldn't create calling convention string");
  }
  output << "call_" << callingConvention.getValue() << "_shim";
  return success();
}

static LogicalResult printFuncOpArguments(IREE::VM::FuncOp &funcOp,
                                          mlir::emitc::CppEmitter &emitter) {
  return mlir::emitc::interleaveCommaWithError(
      funcOp.getArguments(), emitter.ostream(), [&](auto arg) -> LogicalResult {
        if (failed(emitter.emitType(arg.getType()))) {
          return failure();
        }
        emitter.ostream() << " " << emitter.getOrCreateName(arg);
        return success();
      });
}

// Function results get propagated through pointer arguments
static LogicalResult printFuncOpResults(
    IREE::VM::FuncOp &funcOp, mlir::emitc::CppEmitter &emitter,
    SmallVector<std::string, 4> &resultNames) {
  return mlir::emitc::interleaveCommaWithError(
      llvm::zip(funcOp.getType().getResults(), resultNames), emitter.ostream(),
      [&](std::tuple<Type, std::string> tuple) -> LogicalResult {
        Type type = std::get<0>(tuple);
        std::string resultName = std::get<1>(tuple);

        if (failed(emitter.emitType(type))) {
          return failure();
        }
        emitter.ostream() << " *" << resultName;
        return success();
      });
}

static LogicalResult initializeGlobals(IREE::VM::ModuleOp moduleOp,
                                       mlir::emitc::CppEmitter &emitter) {
  llvm::raw_ostream &output = emitter.ostream();

  for (auto globalOp : moduleOp.getOps<IREE::VM::GlobalI32Op>()) {
    Optional<Attribute> initialValue = globalOp.initial_value();
    Optional<StringRef> initializer = globalOp.initializer();
    if (initialValue.hasValue()) {
      // TODO(simon-camp): We can't represent structs in emitc (yet maybe), so
      // the struct argument name here must not be changed.
      emitter.ostream() << "vm_global_store_i32(state->rwdata, "
                        << globalOp.ordinal() << ", ";
      if (failed(emitter.emitAttribute(initialValue.getValue()))) {
        return globalOp.emitError() << "Unable to emit initial_value";
      }
      emitter.ostream() << ");\n";
    } else if (initializer.hasValue()) {
      return globalOp.emitError()
             << "Initializers for globals not supported yet";
    }
  }

  // TODO(simon-camp): Support vm.global.i64 and vm.global.ref

  return success();
}

static LogicalResult translateCallOpToC(IREE::VM::CallOp callOp,
                                        mlir::emitc::CppEmitter &emitter) {
  return success();
}

static LogicalResult translateReturnOpToC(
    IREE::VM::ReturnOp returnOp, mlir::emitc::CppEmitter &emitter,
    SmallVector<std::string, 4> resultNames) {
  for (std::tuple<Value, std::string> tuple :
       llvm::zip(returnOp.getOperands(), resultNames)) {
    Value operand = std::get<0>(tuple);
    std::string resultName = std::get<1>(tuple);
    emitter.ostream() << "*" << resultName << " = "
                      << emitter.getOrCreateName(operand) << ";\n";
  }

  emitter.ostream() << "return iree_ok_status();\n";

  return success();
}

static LogicalResult translateOpToC(Operation &op,
                                    mlir::emitc::CppEmitter &emitter,
                                    SmallVector<std::string, 4> resultNames) {
  if (auto callOp = dyn_cast<IREE::VM::CallOp>(op))
    return translateCallOpToC(callOp, emitter);
  if (auto returnOp = dyn_cast<IREE::VM::ReturnOp>(op))
    return translateReturnOpToC(returnOp, emitter, resultNames);
  // Fall back to generic emitc printer
  if (succeeded(emitter.emitOperation(op))) {
    return success();
  }

  return failure();
}

static LogicalResult translateFunctionToC(IREE::VM::ModuleOp &moduleOp,
                                          IREE::VM::FuncOp &funcOp,
                                          mlir::emitc::CppEmitter &emitter) {
  std::string moduleName = moduleOp.getName().str();
  emitc::CppEmitter::Scope scope(emitter);
  llvm::raw_ostream &output = emitter.ostream();

  // this function later gets wrapped with argument marshalling code
  std::string functionName =
      buildFunctionName(moduleOp, funcOp, /*implSuffix=*/true);

  output << "iree_status_t " << functionName << "(";

  if (failed(printFuncOpArguments(funcOp, emitter))) {
    return failure();
  }

  if (funcOp.getNumResults() > 0 && funcOp.getNumArguments() > 0) {
    output << ", ";
  }

  SmallVector<std::string, 4> resultNames;
  for (unsigned int idx = 0; idx < funcOp.getNumResults(); idx++) {
    std::string resultName = "out" + std::to_string(idx);
    resultNames.push_back(resultName);
  }

  if (failed(printFuncOpResults(funcOp, emitter, resultNames))) {
    return failure();
  }

  if (funcOp.getNumArguments() + funcOp.getNumResults() > 0) {
    output << ", ";
  }

  // TODO(simon-camp): We can't represent structs in emitc (yet maybe), so the
  // struct argument name here must not be changed.
  output << moduleName << "_state_t* state) {\n";

  for (auto &op : funcOp.getOps()) {
    if (failed(translateOpToC(op, emitter, resultNames))) {
      return failure();
    }
  }

  output << "}\n";

  return success();
}

static LogicalResult buildModuleDescriptors(IREE::VM::ModuleOp &moduleOp,
                                            mlir::emitc::CppEmitter &emitter) {
  SymbolTable symbolTable(moduleOp);
  std::string moduleName = moduleOp.getName().str();
  llvm::raw_ostream &output = emitter.ostream();

  // function wrapper
  for (auto funcOp : moduleOp.getOps<IREE::VM::FuncOp>()) {
    output << "static iree_status_t "
           << buildFunctionName(moduleOp, funcOp,
                                /*implSufffix=*/false)
           << "("
           << "iree_vm_stack_t* stack, " << moduleName << "_t* module, "
           << moduleName << "_state_t* state";

    if (funcOp.getNumArguments() > 0) {
      output << ", ";
    }

    if (failed(printFuncOpArguments(funcOp, emitter))) {
      return failure();
    }

    if (funcOp.getNumArguments() > 0) {
      output << ", ";
    }

    SmallVector<std::string, 4> resultNames;
    for (unsigned int idx = 0; idx < funcOp.getNumResults(); idx++) {
      std::string resultName = "out" + std::to_string(idx);
      resultNames.push_back(resultName);
    }

    if (failed(printFuncOpResults(funcOp, emitter, resultNames))) {
      return failure();
    }
    output << ") {\n"
           << "return "
           << buildFunctionName(moduleOp, funcOp,
                                /*implSufffix=*/true)
           << "(";

    SmallVector<std::string, 4> argNames;
    for (Value &argument : funcOp.getArguments()) {
      std::string argName = emitter.getOrCreateName(argument).str();
      argNames.push_back(argName);
    }

    output << llvm::join(argNames, ", ");

    if (funcOp.getNumResults() > 0) {
      output << ", ";
    }

    output << llvm::join(resultNames, ", ");

    if (funcOp.getNumArguments() + funcOp.getNumResults() > 0) {
      output << ", ";
    }
    output << "state);\n}\n";
  }

  auto printCStringView = [](std::string s) -> std::string {
    return "iree_make_cstring_view(\"" + s + "\")";
  };

  // exports
  // TODO: Add sorting the module export table by name. This is already
  //       supported in the ByteCodeModuleTarget using `llvm::sort`.
  std::string exportName = moduleName + "_exports_";
  output << "static const iree_vm_native_export_descriptor_t " << exportName
         << "[] = {\n";
  for (auto exportOp : moduleOp.getOps<IREE::VM::ExportOp>()) {
    auto funcOp = symbolTable.lookup<IREE::VM::FuncOp>(exportOp.function_ref());
    if (!funcOp) {
      return exportOp.emitError("Couldn't find referenced FuncOp");
    }
    auto callingConvention = makeCallingConventionString(funcOp);
    if (!callingConvention) {
      return exportOp.emitError(
          "Couldn't create calling convention string for referenced FuncOp");
    }

    // TODO(simon-camp): support function-level reflection attributes
    output << "{" << printCStringView(exportOp.export_name().str()) << ", "
           << printCStringView(callingConvention.getValue()) << ", 0, NULL},\n";
  }
  output << "};\n";
  output << "\n";

  // imports
  std::string importName = moduleName + "_imports_";
  output << "static const iree_vm_native_import_descriptor_t " << importName
         << "[] = {\n";
  for (auto importOp : moduleOp.getOps<IREE::VM::ImportOp>()) {
    output << "{" << printCStringView(importOp.getName().str()) << "},\n";
  }
  output << "};\n";
  output << "\n";

  // functions
  std::string functionName = moduleName + "_funcs_";
  output << "static const iree_vm_native_function_ptr_t " << functionName
         << "[] = {\n";
  for (auto funcOp : moduleOp.getOps<IREE::VM::FuncOp>()) {
    output << "{"
           << "(iree_vm_native_function_shim_t)";

    if (failed(printShim(funcOp, output))) {
      return funcOp.emitError("Couldn't create calling convention string");
    }
    output << ", "
           << "(iree_vm_native_function_target_t)"
           << buildFunctionName(moduleOp, funcOp, /*implSufffix=*/false)
           << "},\n";
  }
  output << "};\n";
  output << "\n";

  // module descriptor
  // TODO(simon-camp): support module-level reflection attributes
  std::string descriptorName = moduleName + "_descriptor_";
  output << "static const iree_vm_native_module_descriptor_t " << descriptorName
         << " = {\n"
         << printCStringView(moduleName) << ",\n"
         << "IREE_ARRAYSIZE(" << importName << "),\n"
         << importName << ",\n"
         << "IREE_ARRAYSIZE(" << exportName << "),\n"
         << exportName << ",\n"
         << "IREE_ARRAYSIZE(" << functionName << "),\n"
         << functionName << ",\n"
         << "0,\n"
         << "NULL,\n"
         << "};\n";

  // destroy
  // TODO(simon-camp):

  // alloc_state
  output << "static iree_status_t " << moduleName
         << "_alloc_state(void* self, iree_allocator_t allocator, "
            "iree_vm_module_state_t** out_module_state) {\n"
         << moduleName << "_state_t* state = NULL;\n"
         << "IREE_RETURN_IF_ERROR(iree_allocator_malloc(allocator, "
            "sizeof(*state), (void**)&state));\n "
         << "memset(state, 0, sizeof(*state));\n"
         << "state->allocator = allocator;\n";

  // initialize globals
  if (failed(initializeGlobals(moduleOp, emitter))) {
    return moduleOp.emitError() << "Failed to emit global initialization";
  }

  output << "*out_module_state = (iree_vm_module_state_t*)state;\n"
         << "return iree_ok_status();\n"
         << "}\n";

  // free_state
  output << "static void " << moduleName
         << "_free_state(void* self, iree_vm_module_state_t* "
            "module_state) {\n"
         << moduleName << "_state_t* state = (" << moduleName
         << "_state_t*)module_state;\n"
         << "iree_allocator_free(state->allocator, state);\n"
         << "}\n";

  // resolve_imports
  // TODO(simon-camp):

  // create
  output << "static iree_status_t " << moduleName << "_create("
         << "iree_allocator_t allocator, iree_vm_module_t** "
            "out_module) {\n"
         << "iree_vm_module_t interface;\n"
         << "IREE_RETURN_IF_ERROR(iree_vm_module_initialize(&interface, "
            "NULL));\n"
         << "interface.destroy = NULL;\n"
         << "interface.alloc_state = " << moduleName << "_alloc_state;\n"
         << "interface.free_state = " << moduleName << "_free_state;\n"
         << "interface.resolve_import = NULL;\n"
         << "return iree_vm_native_module_create(&interface, "
            "&"
         << descriptorName << ", allocator, out_module);\n"
         << "}\n";

  output << "\n";
  return success();
}

// Adapted from BytecodeModuleTarget and extended by C specific passes
static LogicalResult canonicalizeModule(
    IREE::VM::ModuleOp moduleOp, IREE::VM::CTargetOptions targetOptions) {
  OwningRewritePatternList patterns(&getContext());
  ConversionTarget target(*moduleOp.getContext());
  target.addLegalDialect<IREE::VM::VMDialect>();
  target.addLegalOp<IREE::DoNotOptimizeOp>();

  // Add all VM canonicalization patterns and mark pseudo-ops illegal.
  auto *context = moduleOp.getContext();
  for (auto *op : context->getRegisteredOperations()) {
    // Non-serializable ops must be removed prior to serialization.
    if (op->hasTrait<OpTrait::IREE::VM::PseudoOp>()) {
      // TODO(simon-camp): reenable pass once support for control flow ops has
      // landed
      // op->getCanonicalizationPatterns(patterns, context);
      // target.setOpAction(OperationName(op->name, context),
      //                    ConversionTarget::LegalizationAction::Illegal);
    }

    // Debug ops must not be present when stripping.
    // TODO(benvanik): add RemoveDisabledDebugOp pattern.
    if (op->hasTrait<OpTrait::IREE::VM::DebugOnly>() &&
        targetOptions.stripDebugOps) {
      target.setOpAction(OperationName(op->name, context),
                         ConversionTarget::LegalizationAction::Illegal);
    }
  }

  if (failed(applyFullConversion(moduleOp, target, std::move(patterns)))) {
    return moduleOp.emitError() << "unable to fully apply conversion to module";
  }

  PassManager passManager(context);
  mlir::applyPassManagerCLOptions(passManager);
  auto &modulePasses = passManager.nest<IREE::VM::ModuleOp>();

  if (targetOptions.optimize) {
    // TODO(benvanik): does this run until it quiesces?
    // TODO(simon-camp): reenable pass once support for control flow ops has
    // landed
    // modulePasses.addPass(mlir::createInlinerPass());
    modulePasses.addPass(mlir::createCSEPass());
    // modulePasses.addPass(mlir::createCanonicalizerPass());
  }

  // In the the Bytecode module the order is:
  // * `createDropCompilerHintsPass()`
  // * `IREE::VM::createOrdinalAllocationPass()`
  // Here, we have to reverse the order and run `createConvertVMToEmitCPass()`
  // inbetween to test the EmitC pass. Otherwise, the constants get folded
  // by the canonicalizer.

  // Mark up the module with ordinals for each top-level op (func, etc).
  // This will make it easier to correlate the MLIR textual output to the
  // binary output.
  // We don't want any more modifications after this point as they could
  // invalidate the ordinals.
  modulePasses.addPass(IREE::VM::createOrdinalAllocationPass());

  // C target specific passes
  modulePasses.addPass(createConvertVMToEmitCPass());

  modulePasses.addPass(createDropCompilerHintsPass());

  if (failed(passManager.run(moduleOp->getParentOfType<mlir::ModuleOp>()))) {
    return moduleOp.emitError() << "failed during transform passes";
  }

  return success();
}

LogicalResult translateModuleToC(IREE::VM::ModuleOp moduleOp,
                                 CTargetOptions targetOptions,
                                 llvm::raw_ostream &output) {
  if (failed(canonicalizeModule(moduleOp, targetOptions))) {
    return moduleOp.emitError()
           << "failed to canonicalize vm.module to a serializable form";
  }

  if (targetOptions.outputFormat == COutputFormat::kMlirText) {
    // Use the standard MLIR text printer.
    moduleOp.getOperation()->print(output);
    output << "\n";
    return success();
  }

  auto printInclude = [&output](std::string include) {
    output << "#include \"" << include << "\"\n";
  };

  printInclude("iree/vm/api.h");
  printInclude("iree/vm/ops.h");
  printInclude("iree/vm/shims.h");
  output << "\n";

  printModuleComment(moduleOp, output);
  output << "\n";

  mlir::emitc::CppEmitter emitter(output, /*restrictToC=*/true,
                                  /*forwardDeclareVariables=*/false);
  mlir::emitc::CppEmitter::Scope scope(emitter);

  // build struct definitions
  if (failed(printStructDefinitions(moduleOp, emitter))) {
    return failure();
  }

  // translate functions
  for (auto funcOp : moduleOp.getOps<IREE::VM::FuncOp>()) {
    if (failed(translateFunctionToC(moduleOp, funcOp, emitter))) {
      return failure();
    }

    output << "\n";
  }

  printSeparatingComment(output);

  printModuleComment(moduleOp, output);
  output << "\n";

  // generate module descriptors
  if (failed(buildModuleDescriptors(moduleOp, emitter))) {
    return failure();
  }

  return success();
}

LogicalResult translateModuleToC(mlir::ModuleOp outerModuleOp,
                                 CTargetOptions targetOptions,
                                 llvm::raw_ostream &output) {
  auto moduleOps = outerModuleOp.getOps<IREE::VM::ModuleOp>();
  if (moduleOps.empty()) {
    return outerModuleOp.emitError()
           << "outer module does not contain a vm.module op";
  }
  return translateModuleToC(*moduleOps.begin(), targetOptions, output);
}

}  // namespace VM
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
