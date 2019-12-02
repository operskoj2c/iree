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

#include "bindings/python/pyiree/compiler.h"

#include <stdexcept>

#include "bindings/python/pyiree/binding.h"
#include "bindings/python/pyiree/status_utils.h"
#include "iree/compiler/Translation/Sequencer/SequencerModuleTranslation.h"
#include "iree/compiler/Utils/TranslationUtils.h"
#include "iree/schemas/module_def_generated.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Location.h"
#include "mlir/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"

namespace py = pybind11;

using namespace mlir;
using namespace mlir::iree_compiler;

using llvm::MemoryBuffer;
using llvm::MemoryBufferRef;
using llvm::StringRef;

namespace iree {
namespace python {

namespace {

OwningModuleRef parseMLIRModuleFromString(StringRef contents,
                                          MLIRContext* context) {
  std::unique_ptr<MemoryBuffer> contents_buffer;
  if (contents.back() == 0) {
    // If it has a nul terminator, just use as-is.
    contents_buffer = MemoryBuffer::getMemBuffer(contents.drop_back());
  } else {
    // Otherwise, make a copy.
    contents_buffer = MemoryBuffer::getMemBufferCopy(contents, "EMBED");
  }

  llvm::SourceMgr source_mgr;
  source_mgr.AddNewSourceBuffer(std::move(contents_buffer), llvm::SMLoc());
  OwningModuleRef mlir_module = parseSourceFile(source_mgr, context);
  return mlir_module;
}

}  // namespace

DiagnosticCapture::DiagnosticCapture(mlir::MLIRContext* mlir_context,
                                     DiagnosticCapture* parent)
    : mlir_context_(mlir_context), parent_(parent) {
  handler_id_ = mlir_context_->getDiagEngine().registerHandler(
      [&](Diagnostic& d) -> LogicalResult {
        diagnostics_.push_back(std::move(d));
        return success();
      });
}
DiagnosticCapture::~DiagnosticCapture() {
  if (mlir_context_) {
    mlir_context_->getDiagEngine().eraseHandler(handler_id_);
    if (parent_) {
      for (auto& d : diagnostics_) {
        parent_->diagnostics_.push_back(std::move(d));
      }
    }
  }
}

DiagnosticCapture::DiagnosticCapture(DiagnosticCapture&& other) {
  mlir_context_ = other.mlir_context_;
  parent_ = other.parent_;
  diagnostics_.swap(other.diagnostics_);
  handler_id_ = other.handler_id_;
  other.mlir_context_ = nullptr;
}

// Custom location printer that prints prettier, multi-line file output
// suitable for human readable error messages. The standard printer just prints
// a long nested expression not particularly human friendly). Note that there
// is a location pretty printer in the MLIR AsmPrinter. It is private and
// doesn't do any path shortening, which seems to make long Python stack traces
// a bit easier to scan.
void PrintLocation(Location loc, llvm::raw_ostream& out) {
  switch (loc->getKind()) {
    case StandardAttributes::OpaqueLocation:
      PrintLocation(loc.cast<OpaqueLoc>().getFallbackLocation(), out);
      break;
    case StandardAttributes::UnknownLocation:
      out << "  [unknown location]\n";
      break;
    case StandardAttributes::FileLineColLocation: {
      auto line_col_loc = loc.cast<FileLineColLoc>();
      StringRef this_filename = line_col_loc.getFilename();
      auto slash_pos = this_filename.find_last_of("/\\");
      bool has_basename = false;
      StringRef basename = this_filename;
      if (slash_pos != StringRef::npos) {
        has_basename = true;
        basename = this_filename.substr(slash_pos + 1);
      }
      out << "  at: " << basename << " [" << line_col_loc.getLine() << ":"
          << line_col_loc.getColumn() << "]";
      if (has_basename) {
        out << "\t(" << this_filename << ")";
      }
      out << "\n";
      break;
    }
    case StandardAttributes::NameLocation: {
      auto nameLoc = loc.cast<NameLoc>();
      out << "  @'" << nameLoc.getName() << "':\n";
      auto childLoc = nameLoc.getChildLoc();
      if (!childLoc.isa<UnknownLoc>()) {
        out << "(...\n";
        PrintLocation(childLoc, out);
        out << ")\n";
      }
      break;
    }
    case StandardAttributes::CallSiteLocation: {
      auto call_site = loc.cast<CallSiteLoc>();
      PrintLocation(call_site.getCaller(), out);
      PrintLocation(call_site.getCallee(), out);
      break;
    }
  }
}

std::string DiagnosticCapture::ConsumeDiagnosticsAsString(
    const char* error_message) {
  std::string s;
  llvm::raw_string_ostream sout(s);
  bool first = true;
  if (error_message) {
    sout << error_message;
    first = false;
  }
  for (auto& d : diagnostics_) {
    if (!first) {
      sout << "\n\n";
    } else {
      first = false;
    }

    switch (d.getSeverity()) {
      case DiagnosticSeverity::Note:
        sout << "[NOTE]";
        break;
      case DiagnosticSeverity::Warning:
        sout << "[WARNING]";
        break;
      case DiagnosticSeverity::Error:
        sout << "[ERROR]";
        break;
      case DiagnosticSeverity::Remark:
        sout << "[REMARK]";
        break;
      default:
        sout << "[UNKNOWN]";
    }
    // Message.
    sout << ": " << d << "\n";
    PrintLocation(d.getLocation(), sout);
  }

  diagnostics_.clear();
  return sout.str();
}

void DiagnosticCapture::ClearDiagnostics() { diagnostics_.clear(); }

CompilerContextBundle::CompilerContextBundle()
    : default_capture_(&mlir_context_, nullptr) {}
CompilerContextBundle::~CompilerContextBundle() = default;

CompilerModuleBundle CompilerContextBundle::ParseAsm(
    const std::string& asm_text) {
  // Arrange to get a view that includes a terminating null to avoid additional
  // copy.
  const char* asm_chars = asm_text.c_str();
  StringRef asm_sr(asm_chars, asm_text.size() + 1);

  auto diag_capture = CaptureDiagnostics();
  auto module_ref = parseMLIRModuleFromString(asm_sr, mlir_context());
  if (!module_ref) {
    throw RaiseValueError(
        diag_capture.ConsumeDiagnosticsAsString("Error parsing ASM").c_str());
  }
  return CompilerModuleBundle(shared_from_this(), module_ref.release());
}

std::string CompilerModuleBundle::ToAsm(bool enableDebugInfo, bool prettyForm,
                                        int64_t largeElementLimit) {
  // Print to asm.
  std::string asm_output;
  llvm::raw_string_ostream sout(asm_output);
  OpPrintingFlags print_flags;
  if (enableDebugInfo) {
    print_flags.enableDebugInfo(prettyForm);
  }
  if (largeElementLimit >= 0) {
    print_flags.elideLargeElementsAttrs(largeElementLimit);
  }
  module_op().print(sout, print_flags);
  return sout.str();
}

std::shared_ptr<OpaqueBlob> CompilerModuleBundle::CompileToSequencerBlob(
    bool print_mlir, const std::string& crash_reproducer,
    std::vector<std::string> target_backends) {
  ModuleTranslationOptions options;
  options.print_mlir = print_mlir;
  options.crash_reproducer = crash_reproducer;
  options.target_backends = std::move(target_backends);

  auto diag_capture = context_->CaptureDiagnostics();
  auto module_blob = mlir::iree_compiler::translateMlirToIreeSequencerModule(
      module_op(), options);
  if (module_blob.empty()) {
    throw RaiseValueError(
        diag_capture
            .ConsumeDiagnosticsAsString("Failed to translate MLIR module")
            .c_str());
  }
  return std::make_shared<OpaqueByteVectorBlob>(std::move(module_blob));
}

void CompilerModuleBundle::RunPassPipeline(
    const std::vector<std::string>& pipelines,
    const std::string& crash_reproducer) {
  mlir::PassManager pm(context_->mlir_context());
  if (!crash_reproducer.empty()) {
    pm.enableCrashReproducerGeneration(crash_reproducer);
  }

  // Parse the pass pipelines.
  std::string error;
  llvm::raw_string_ostream error_stream(error);
  for (const auto& pipeline : pipelines) {
    if (failed(mlir::parsePassPipeline(pipeline, pm, error_stream))) {
      throw RaiseValueError(error_stream.str().c_str());
    }
  }

  // Run them.
  auto diag_capture = context_->CaptureDiagnostics();
  if (failed(pm.run(module_op_))) {
    throw RaisePyError(
        PyExc_RuntimeError,
        diag_capture.ConsumeDiagnosticsAsString("Error running pass pipelines:")
            .c_str());
  }
}

void SetupCompilerBindings(pybind11::module m) {
  py::class_<CompilerContextBundle, std::shared_ptr<CompilerContextBundle>>(
      m, "CompilerContext")
      .def(py::init<>([]() {
        // Need explicit make_shared to avoid UB with enable_shared_from_this.
        return std::make_shared<CompilerContextBundle>();
      }))
      .def("parse_asm", &CompilerContextBundle::ParseAsm)
      .def("get_diagnostics",
           &CompilerContextBundle::ConsumeDiagnosticsAsString)
      .def("clear_diagnostics", &CompilerContextBundle::ClearDiagnostics);
  py::class_<CompilerModuleBundle>(m, "CompilerModule")
      .def("to_asm", &CompilerModuleBundle::ToAsm,
           py::arg("debug_info") = false, py::arg("pretty") = false,
           py::arg("large_element_limit") = -1)
      .def("compile_to_sequencer_blob",
           &CompilerModuleBundle::CompileToSequencerBlob,
           py::arg("print_mlir") = false, py::arg("crash_reproducer") = "",
           py::arg("target_backends") = std::vector<std::string>())
      .def("run_pass_pipeline", &CompilerModuleBundle::RunPassPipeline,
           py::arg("pipelines") = std::vector<std::string>(),
           py::arg("crash_reproducer") = "");
}

}  // namespace python
}  // namespace iree
