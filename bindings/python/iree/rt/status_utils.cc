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

#include "bindings/python/iree/rt/status_utils.h"

#include "absl/strings/str_cat.h"

namespace iree {
namespace python {

namespace {

PyObject* ApiStatusToPyExcClass(iree_status_t status) {
  switch (iree_status_code(status)) {
    case IREE_STATUS_INVALID_ARGUMENT:
      return PyExc_ValueError;
    case IREE_STATUS_OUT_OF_RANGE:
      return PyExc_IndexError;
    case IREE_STATUS_UNIMPLEMENTED:
      return PyExc_NotImplementedError;
    default:
      return PyExc_RuntimeError;
  }
}

}  // namespace

pybind11::error_already_set ApiStatusToPyExc(iree_status_t status,
                                             const char* message) {
  assert(!iree_status_is_ok(status));
  std::string full_message;

  char* iree_message;
  size_t iree_message_length;
  if (iree_status_to_string(status, &iree_message, &iree_message_length)) {
    full_message = absl::StrCat(
        message, ": ", absl::string_view(iree_message, iree_message_length));
    iree_allocator_free(iree_allocator_system(), iree_message);
  } else {
    full_message = absl::StrCat(
        message, ": ", iree_status_code_string(iree_status_code(status)));
  }

  PyErr_SetString(ApiStatusToPyExcClass(status), full_message.c_str());
  iree_status_ignore(status);
  return pybind11::error_already_set();
}

pybind11::error_already_set RaisePyError(PyObject* exc_class,
                                         const char* message) {
  PyErr_SetString(exc_class, message);
  return pybind11::error_already_set();
}

}  // namespace python
}  // namespace iree
