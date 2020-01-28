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

#include "iree/modules/strings/dialect/conversion_patterns.h"

#include "iree/compiler/Dialect/VM/Conversion/ImportUtils.h"
#include "iree/modules/strings/dialect/strings_dialect.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Strings {

void populateStringsToVMPatterns(MLIRContext *context,
                                 SymbolTable &importSymbols,
                                 OwningRewritePatternList &patterns,
                                 TypeConverter &typeConverter) {
  patterns.insert<VMImportOpConversion<IREE::Strings::I32ToStringOp>>(
      context, importSymbols, typeConverter, "strings.i32_to_string");
  patterns.insert<VMImportOpConversion<IREE::Strings::PrintOp>>(
      context, importSymbols, typeConverter, "strings.print");
}

}  // namespace Strings
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
