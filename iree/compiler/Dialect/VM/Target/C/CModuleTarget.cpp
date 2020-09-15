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
#include "iree/compiler/Dialect/VM/Conversion/VMToEmitC/ConvertVMToEmitC.h"
#include "mlir/Pass/PassManager.h"

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
  output << "\n";
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

static LogicalResult translateCallOpToC(mlir::emitc::CppEmitter &emitter,
                                        IREE::VM::CallOp callOp,
                                        llvm::raw_ostream &output) {
  return success();
}

static LogicalResult translateReturnOpToC(
    mlir::emitc::CppEmitter &emitter, IREE::VM::ReturnOp returnOp,
    llvm::raw_ostream &output, std::vector<std::string> resultNames) {
  for (std::tuple<Value, std::string> tuple :
       llvm::zip(returnOp.getOperands(), resultNames)) {
    Value operand = std::get<0>(tuple);
    std::string resultName = std::get<1>(tuple);
    output << "*" << resultName << " = " << emitter.getOrCreateName(operand)
           << ";\n";
  }

  output << "return iree_ok_status();\n";

  return success();
}

static LogicalResult translateOpToC(mlir::emitc::CppEmitter &emitter,
                                    Operation &op, llvm::raw_ostream &output,
                                    std::vector<std::string> resultNames) {
  if (auto callOp = dyn_cast<IREE::VM::CallOp>(op))
    return translateCallOpToC(emitter, callOp, output);
  if (auto returnOp = dyn_cast<IREE::VM::ReturnOp>(op))
    return translateReturnOpToC(emitter, returnOp, output, resultNames);
  if (succeeded(emitter.emitOperation(op))) {
    return success();
  }

  return failure();
}

static LogicalResult translateFunctionToC(mlir::emitc::CppEmitter &emitter,
                                          IREE::VM::ModuleOp &moduleOp,
                                          IREE::VM::FuncOp &funcOp,
                                          llvm::raw_ostream &output) {
  emitc::CppEmitter::Scope scope(emitter);

  // this function later gets wrapped with argument marshalling code
  std::string functionName =
      buildFunctionName(moduleOp, funcOp, /*implSuffix=*/true);

  output << "iree_status_t " << functionName << "(";

  mlir::emitc::interleaveCommaWithError(
      funcOp.getArguments(), output, [&](auto arg) -> LogicalResult {
        if (failed(emitter.emitType(arg.getType()))) {
          return failure();
        }
        output << " " << emitter.getOrCreateName(arg);
        return success();
      });

  if (funcOp.getNumResults() > 0) {
    output << ", ";
  }

  std::vector<std::string> resultNames;
  for (size_t idx = 0; idx < funcOp.getNumResults(); idx++) {
    std::string resultName("out");
    resultName.append(std::to_string(idx));
    resultNames.push_back(resultName);
  }

  mlir::emitc::interleaveCommaWithError(
      llvm::zip(funcOp.getType().getResults(), resultNames), output,
      [&](std::tuple<Type, std::string> tuple) -> LogicalResult {
        Type type = std::get<0>(tuple);
        std::string resultName = std::get<1>(tuple);

        if (failed(emitter.emitType(type))) {
          return failure();
        }
        output << " *" << resultName;
        return success();
      });

  output << ") {\n";

  for (auto &op : funcOp.getOps()) {
    if (failed(translateOpToC(emitter, op, output, resultNames))) {
      return failure();
    }
  }

  output << "}\n";

  return success();
}

static LogicalResult buildModuleDescriptors(IREE::VM::ModuleOp &moduleOp,
                                            llvm::raw_ostream &output) {
  std::string moduleName = moduleOp.getName().str();

  // exports
  std::string exportName = moduleName + "_exports_";
  // TODO(marbre/simon-camp): Fix generating the calling_convention.
  /*
  output << "static const iree_vm_native_export_descriptor_t " << exportName
         << "[] = {\n";
  for (auto exportOp : moduleOp.getOps<IREE::VM::ExportOp>()) {
    // TODO(simon-camp) support function-level reflection attributes
    output << "{iree_make_cstring_view(\"" << exportOp.export_name()
           << "\"), 0, 0, 0, NULL},\n";
  }
  output << "};\n";
  output << "\n";
  */

  // imports
  std::string importName = moduleName + "_imports_";
  output << "static const iree_vm_native_import_descriptor_t " << importName
         << "[] = {\n";
  for (auto importOp : moduleOp.getOps<IREE::VM::ImportOp>()) {
    output << "{iree_make_cstring_view(\"" << importOp.getName() << "\")},\n";
  }
  output << "};\n";
  output << "\n";

  // functions
  std::string functionName = moduleName + "_funcs_";

  // module descriptor
  // TODO(simon-camp): support module-level reflection attributes
  // TODO(marbre/simon-camp): Renable after fix generating the export descriptor
  /*
  std::string descriptorName = moduleName + "_descriptor_";
  output << "static const iree_vm_native_module_descriptor_t " << descriptorName
         << " = {\n"
         << "iree_make_cstring_view(\"" << moduleName << "\"),\n"
         << "IREE_ARRAYSIZE(" << importName << "),\n"
         << importName << ",\n"
         << "IREE_ARRAYSIZE(" << exportName << "),\n"
         << exportName << ",\n"
         << "IREE_ARRAYSIZE(" << functionName << "),\n"
         << functionName << ",\n"
         << "0,\n"
         << "NULL,\n"
         << "};\n";
  */

  // TODO(simon-camp) generate boilerplate code
  //   * module struct
  //   * module state struct
  //   * function wrappers
  //   * function table
  //   * interface functions
  //      * create
  //      * destroy
  //      * alloc_state
  //      * free_state
  //      * resolve_import

  output << "\n";
  return success();
}

LogicalResult translateModuleToC(mlir::ModuleOp outerModuleOp,
                                 llvm::raw_ostream &output) {
  PassManager pm(outerModuleOp.getContext());

  pm.addPass(createConvertVMToEmitCPass());

  if (failed(pm.run(outerModuleOp))) {
    return failure();
  }

  auto moduleOps = outerModuleOp.getOps<ModuleOp>();
  if (moduleOps.empty()) {
    return outerModuleOp.emitError()
           << "outer module does not contain a vm.module op";
  }

  auto printInlcude = [&output](std::string include) {
    output << "#include \"" << include << "\"\n";
  };

  printInlcude("iree/vm/context.h");
  printInlcude("iree/vm/instance.h");
  printInlcude("iree/vm/native_module.h");
  printInlcude("iree/vm/ref.h");
  printInlcude("iree/vm/stack.h");
  output << "\n";

  printInlcude("iree/vm/vm_c_funcs.h");
  output << "\n";

  for (auto moduleOp : moduleOps) {
    printModuleComment(moduleOp, output);

    mlir::emitc::CppEmitter emitter(output);
    mlir::emitc::CppEmitter::Scope scope(emitter);

    // translate functions
    for (auto funcOp : moduleOp.getOps<IREE::VM::FuncOp>()) {
      if (failed(translateFunctionToC(emitter, moduleOp, funcOp, output))) {
        return failure();
      }

      output << "\n";
    }
  }

  printSeparatingComment(output);

  for (auto moduleOp : moduleOps) {
    printModuleComment(moduleOp, output);

    // generate module descriptors
    if (failed(buildModuleDescriptors(moduleOp, output))) {
      return failure();
    }
  }
  return success();
}

}  // namespace VM
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
