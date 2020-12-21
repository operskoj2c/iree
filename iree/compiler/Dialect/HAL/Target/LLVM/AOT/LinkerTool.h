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
//

#ifndef IREE_COMPILER_DIALECT_HAL_TARGET_LLVM_AOT_LINKERTOOL_H_
#define IREE_COMPILER_DIALECT_HAL_TARGET_LLVM_AOT_LINKERTOOL_H_

#include <string>

#include "iree/compiler/Dialect/HAL/Target/LLVM/LLVMTargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

struct Artifact {
  // Creates an output file path/container pair.
  // By default the file will be deleted when the link completes; callers must
  // use llvm::ToolOutputFile::keep() to prevent deletion upon success (or if
  // leaving artifacts for debugging).
  static Artifact createTemporary(StringRef prefix, StringRef suffix);

  // Creates an output file derived from the given file's path with a new
  // suffix.
  static Artifact createVariant(StringRef basePath, StringRef suffix);

  Artifact() = default;
  Artifact(std::string path, std::unique_ptr<llvm::ToolOutputFile> outputFile)
      : path(std::move(path)), outputFile(std::move(outputFile)) {}

  std::string path;
  std::unique_ptr<llvm::ToolOutputFile> outputFile;

  // Reads the artifact file contents as bytes.
  Optional<std::vector<int8_t>> read() const;

  // Reads the artifact file and writes it into the given |stream|.
  bool readInto(raw_ostream& targetStream) const;

  // Closes the ostream of the file while preserving the temporary entry on
  // disk. Use this if files need to be modified by external tools that may
  // require exclusive access.
  void close();
};

struct Artifacts {
  // File containing the linked library (DLL, ELF, etc).
  Artifact libraryFile;

  // Optional file containing associated debug information (if stored
  // separately, such as PDB files).
  Artifact debugFile;

  // Other files associated with linking.
  SmallVector<Artifact, 4> otherFiles;

  // Keeps all of the artifacts around after linking completes. Useful for
  // debugging.
  void keepAllFiles();
};

// Base type for linker tools that can turn object files into shared objects.
class LinkerTool {
 public:
  // Gets an instance of a linker tool for the given target options. This may
  // be a completely different toolchain than that of the host.
  static std::unique_ptr<LinkerTool> getForTarget(
      llvm::Triple& targetTriple, LLVMTargetOptions& targetOptions);

  explicit LinkerTool(llvm::Triple targetTriple,
                      LLVMTargetOptions targetOptions)
      : targetTriple(std::move(targetTriple)),
        targetOptions(std::move(targetOptions)) {}

  virtual ~LinkerTool() = default;

  // Returns the path to the linker tool binary.
  virtual std::string getToolPath() const;

  // Configures a module prior to compilation with any additional
  // functions/exports it may need, such as shared object initializer functions.
  virtual LogicalResult configureModule(
      llvm::Module* llvmModule, ArrayRef<StringRef> entryPointNames) = 0;

  // Links the given object files into a dynamically loadable library.
  // The resulting library (and other associated artifacts) will be returned on
  // success.
  virtual Optional<Artifacts> linkDynamicLibrary(
      StringRef libraryName, ArrayRef<Artifact> objectFiles) = 0;

 protected:
  // Runs the given command line on the shell, logging failures.
  LogicalResult runLinkCommand(const std::string& commandLine);

  llvm::Triple targetTriple;
  LLVMTargetOptions targetOptions;
};

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_HAL_TARGET_LLVM_AOT_LINKERTOOL_H_
