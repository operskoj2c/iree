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

#ifndef THIRD_PARTY_MLIR_EDGE_IREE_VM_OPCODE_INFO_H_
#define THIRD_PARTY_MLIR_EDGE_IREE_VM_OPCODE_INFO_H_

#include "third_party/absl/strings/string_view.h"
#include "third_party/absl/types/optional.h"
#include "third_party/absl/types/span.h"
#include "third_party/mlir_edge/iree/schemas/bytecode/bytecode_v0.h"

namespace iree {
namespace vm {

struct OpcodeInfo {
  const char* mnemonic;
  OpcodeFlagBitfield flag;
  union {
    const char operands_value[8];
    const OperandEncoding operands[8];
  };
};

using OpcodeTable = absl::Span<const OpcodeInfo>;

template <typename T>
inline const OpcodeInfo& GetOpcodeInfo(OpcodeTable opcode_table, T opcode) {
  return opcode_table[static_cast<uint8_t>(opcode)];
}

}  // namespace vm
}  // namespace iree

#endif  // THIRD_PARTY_MLIR_EDGE_IREE_VM_OPCODE_INFO_H_
