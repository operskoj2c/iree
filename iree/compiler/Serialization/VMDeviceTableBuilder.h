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

#ifndef IREE_COMPILER_SERIALIZATION_VMDEVICETABLEBUILDER_H_
#define IREE_COMPILER_SERIALIZATION_VMDEVICETABLEBUILDER_H_

#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"
#include "iree/schemas/device_table_def_generated.h"
#include "third_party/llvm/llvm/projects/google_mlir/include/mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {

class VMDeviceTableBuilder {
 public:
  explicit VMDeviceTableBuilder(::flatbuffers::FlatBufferBuilder *fbb);

  LogicalResult AddDevice(::flatbuffers::Offset<iree::DeviceDef> deviceDef);

  LogicalResult AddDeviceGroup(
      ::flatbuffers::Offset<iree::DeviceGroupDef> deviceGroupDef);

  ::flatbuffers::Offset<iree::DeviceTableDef> Finish();

 private:
  ::flatbuffers::FlatBufferBuilder *fbb_;
  std::vector<::flatbuffers::Offset<iree::DeviceDef>> deviceDefs_;
  std::vector<::flatbuffers::Offset<iree::DeviceGroupDef>> deviceGroupDefs_;
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_SERIALIZATION_VMDEVICETABLEBUILDER_H_
