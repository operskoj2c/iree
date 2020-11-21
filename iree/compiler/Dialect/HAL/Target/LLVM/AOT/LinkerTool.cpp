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

#include "iree/compiler/Dialect/HAL/Target/LLVM/AOT/LinkerTool.h"

#define DEBUG_TYPE "llvmaot-linker"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

// static
Artifact Artifact::createTemporary(StringRef prefix, StringRef suffix) {
  llvm::SmallString<32> filePath;
  if (std::error_code error =
          llvm::sys::fs::createTemporaryFile(prefix, suffix, filePath)) {
    llvm::errs() << "failed to generate temporary file: " << error.message();
    return {};
  }
  std::error_code error;
  auto file = std::make_unique<llvm::ToolOutputFile>(filePath, error,
                                                     llvm::sys::fs::OF_None);
  if (error) {
    llvm::errs() << "failed to open temporary file '" << filePath
                 << "': " << error.message();
    return {};
  }
  return {filePath.str().str(), std::move(file)};
}

// static
Artifact Artifact::createVariant(StringRef basePath, StringRef suffix) {
  SmallString<32> filePath(basePath);
  llvm::sys::path::replace_extension(filePath, suffix);
  std::error_code error;
  auto file = std::make_unique<llvm::ToolOutputFile>(filePath, error,
                                                     llvm::sys::fs::OF_Append);
  if (error) {
    llvm::errs() << "failed to open temporary file '" << filePath
                 << "': " << error.message();
    return {};
  }
  return {filePath.str().str(), std::move(file)};
}

Optional<std::vector<int8_t>> Artifact::read() const {
  auto fileData = llvm::MemoryBuffer::getFile(path);
  if (!fileData) {
    llvm::errs() << "failed to load library output file '" << path << "'";
    return llvm::None;
  }
  auto sourceBuffer = fileData.get()->getBuffer();
  std::vector<int8_t> resultBuffer(sourceBuffer.size());
  std::memcpy(resultBuffer.data(), sourceBuffer.data(), sourceBuffer.size());
  return resultBuffer;
}

bool Artifact::readInto(raw_ostream &targetStream) const {
  // NOTE: we could make this much more efficient if we read in the file a
  // chunk at a time and piped it along to targetStream. I couldn't find
  // anything in LLVM that did this, for some crazy reason, but since we are
  // dealing with binaries that can be 10+MB here it'd be nice if we could avoid
  // reading them all into memory.
  auto fileData = llvm::MemoryBuffer::getFile(path);
  if (!fileData) {
    llvm::errs() << "failed to load library output file '" << path << "'";
    return false;
  }
  auto sourceBuffer = fileData.get()->getBuffer();
  targetStream.write(sourceBuffer.data(), sourceBuffer.size());
  return true;
}

void Artifact::close() { outputFile->os().close(); }

void Artifacts::keepAllFiles() {
  if (libraryFile.outputFile) libraryFile.outputFile->keep();
  if (debugFile.outputFile) debugFile.outputFile->keep();
  for (auto &file : otherFiles) {
    file.outputFile->keep();
  }
}

std::string LinkerTool::getToolPath() const {
  char *linkerPath = std::getenv("IREE_LLVMAOT_LINKER_PATH");
  if (linkerPath) {
    return std::string(linkerPath);
  } else {
    return "";
  }
}

LogicalResult LinkerTool::runLinkCommand(const std::string &commandLine) {
  LLVM_DEBUG(llvm::dbgs() << "Running linker command:\n" << commandLine);
#if defined(_MSC_VER)
  // It's easy to run afoul of quoting rules on Windows (such as when using
  // spaces in the linker environment variable). See:
  // https://stackoverflow.com/a/9965141
  auto quotedCommandLine = "\"" + commandLine + "\"";
  int exitCode = system(quotedCommandLine.c_str());
#else
  int exitCode = system(commandLine.c_str());
#endif  // _MSC_VER
  if (exitCode == 0) return success();
  llvm::errs() << "Linking failed; command line returned exit code " << exitCode
               << ":\n\n"
               << commandLine << "\n\n";
  return failure();
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
