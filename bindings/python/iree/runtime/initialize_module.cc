// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "bindings/python/iree/runtime/binding.h"
#include "bindings/python/iree/runtime/hal.h"
#include "bindings/python/iree/runtime/status_utils.h"
#include "bindings/python/iree/runtime/vm.h"
#include "iree/base/internal/flags.h"
#include "iree/base/status_cc.h"
#include "iree/hal/drivers/init.h"

namespace iree {
namespace python {

PYBIND11_MODULE(binding, m) {
  IREE_CHECK_OK(iree_hal_register_all_available_drivers(
      iree_hal_driver_registry_default()));

  m.doc() = "IREE Binding Backend Helpers";
  SetupHalBindings(m);
  SetupVmBindings(m);

  m.def("parse_flags", [](py::args py_flags) {
    std::vector<std::string> alloced_flags;
    alloced_flags.push_back("python");
    for (auto &py_flag : py_flags) {
      alloced_flags.push_back(py::cast<std::string>(py_flag));
    }

    // Must build pointer vector after filling so pointers are stable.
    std::vector<char *> flag_ptrs;
    for (auto &alloced_flag : alloced_flags) {
      flag_ptrs.push_back(const_cast<char *>(alloced_flag.c_str()));
    }

    char **argv = &flag_ptrs[0];
    int argc = flag_ptrs.size();
    CheckApiStatus(
        iree_flags_parse(IREE_FLAGS_PARSE_MODE_DEFAULT, &argc, &argv),
        "Error parsing flags");
  });
}

}  // namespace python
}  // namespace iree
