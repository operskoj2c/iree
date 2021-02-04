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

#include "iree/compiler/Dialect/HAL/Target/LLVM/LinkerTool.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "llvmaot-linker"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

// Unix linker (ld-like); for ELF files.
class UnixLinkerTool : public LinkerTool {
 public:
  using LinkerTool::LinkerTool;

  std::string getToolPath() const override {
    auto toolPath = LinkerTool::getToolPath();
    if (!toolPath.empty()) return toolPath;

// Attempt to find a linker on the path. Ideally a user specifies the linker so
// they can match whatever they are building with.
//
// TODO(ataei): support discovery of linkers on Windows/etc; popen is
// linux-only. There may be infra in LLVM to do this kind of stuff in a
// cross-platform way.
#if !defined(WIN32)
#define UNIX_SYS_LINKER_PATH_LENGTH 255
    auto sysLinkers = {"ld", "ld.gold", "lld.ld"};
    for (auto syslinker : sysLinkers) {
      FILE *pipe =
          popen(llvm::Twine("which ").concat(syslinker).str().c_str(), "r");
      char linkerPath[UNIX_SYS_LINKER_PATH_LENGTH];
      if (fgets(linkerPath, sizeof(linkerPath), pipe) != NULL) {
        return strtok(linkerPath, "\n");
      }
    }
#undef UNIX_SYS_LINKER_PATH_LENGTH
#endif  // WIN32

    return toolPath;
  }

  LogicalResult configureModule(llvm::Module *llvmModule,
                                ArrayRef<StringRef> entryPointNames) override {
    for (auto &func : *llvmModule) {
      // Enable frame pointers to ensure that stack unwinding works.
      func.addFnAttr("frame-pointer", "all");

      // -ffreestanding-like behavior.
      func.addFnAttr("no-builtins");
    }

    // TODO(benvanik): switch to executable libraries w/ internal functions.
    for (auto entryPointName : entryPointNames) {
      auto *entryPointFn = llvmModule->getFunction(entryPointName);
      entryPointFn->setLinkage(
          llvm::GlobalValue::LinkageTypes::ExternalLinkage);
      entryPointFn->setVisibility(
          llvm::GlobalValue::VisibilityTypes::DefaultVisibility);
    }

    return success();
  }

  Optional<Artifacts> linkDynamicLibrary(
      StringRef libraryName, ArrayRef<Artifact> objectFiles) override {
    Artifacts artifacts;

    // Create the shared object name; if we only have a single input object we
    // can just reuse that.
    if (objectFiles.size() == 1) {
      artifacts.libraryFile =
          Artifact::createVariant(objectFiles.front().path, "so");
    } else {
      artifacts.libraryFile = Artifact::createTemporary(libraryName, "so");
    }
    artifacts.libraryFile.close();

    SmallVector<std::string, 8> flags = {
        getToolPath(),

        // Avoids including any libc/startup files that initialize the CRT as
        // we don't use any of that. Our shared libraries must be freestanding.
        "-nostdlib",  // -nodefaultlibs + -nostartfiles

        // Statically link all dependencies so we don't have any runtime deps.
        // We cannot have any imports in the module we produce.
        // "-static",

        // HACK: we insert mallocs and libm calls. This is *not good*.
        // We need hermetic binaries that pull in no imports; the MLIR LLVM
        // lowering paths introduce a bunch, though, so this is what we are
        // stuck with.
        "-shared",
        "-undefined suppress",

        "-o " + artifacts.libraryFile.path,
    };

    if (targetTriple.isOSDarwin() || targetTriple.isiOS()) {
      flags.push_back("-dylib");
      flags.push_back("-flat_namespace");
    }

    // Strip debug information (only, no relocations) when not requested.
    if (!targetOptions.debugSymbols) {
      flags.push_back("-Wl,--strip-debug");
    }

    // Link all input objects. Note that we are not linking whole-archive as
    // we want to allow dropping of unused codegen outputs.
    for (auto &objectFile : objectFiles) {
      flags.push_back(objectFile.path);
    }

    auto commandLine = llvm::join(flags, " ");
    if (failed(runLinkCommand(commandLine))) return llvm::None;
    return artifacts;
  }
};

std::unique_ptr<LinkerTool> createUnixLinkerTool(
    llvm::Triple &targetTriple, LLVMTargetOptions &targetOptions) {
  return std::make_unique<UnixLinkerTool>(targetTriple, targetOptions);
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
