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

#ifndef IREE_COMPILER_DIALECT_HAL_CONVERSION_TYPECONVERTER_H_
#define IREE_COMPILER_DIALECT_HAL_CONVERSION_TYPECONVERTER_H_

#include <vector>

#include "iree/compiler/Dialect/HAL/Conversion/ConversionDialectInterface.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

class HALTypeConverter : public TypeConverter {
 public:
  HALTypeConverter(
      ArrayRef<const HALConversionDialectInterface *> conversionInterfaces);

  // TODO(benvanik): signature conversion for output buffers.

  static bool shouldConvertToHalBuffer(Type type) {
    if (TensorType tensor_type = type.template dyn_cast<TensorType>()) {
      return tensor_type.getElementType().isIntOrFloat();
    }
    return false;
  }

 private:
  // The set of dialect conversion interfaces we should query to convert types.
  std::vector<const HALConversionDialectInterface *> conversionInterfaces;
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_DIALECT_HAL_CONVERSION_TYPECONVERTER_H_
