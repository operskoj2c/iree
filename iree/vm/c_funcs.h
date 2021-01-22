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

#ifndef IREE_VM_C_FUNCS_H_
#define IREE_VM_C_FUNCS_H_

#include <stdint.h>

// Arithmetic ops
inline int32_t vm_add_i32(int32_t a, int32_t b) { return a + b; }

// Check ops
// TODO(simon-camp): These macros should be removed once control flow ops are
// supported in the c module target
#define VM_CHECK_EQ(a, b, message)                                          \
  if (a != b) {                                                             \
    return iree_status_allocate(IREE_STATUS_FAILED_PRECONDITION, "<vm>", 0, \
                                iree_make_cstring_view("message"));         \
  }

// Compare ops
inline int32_t vm_cmp_ne_i32(int32_t a, int32_t b) { return a != b ? 1 : 0; }

// Const ops
inline int32_t vm_const_i32(int32_t a) { return a; }

#endif  // IREE_VM_C_FUNCS_H_
