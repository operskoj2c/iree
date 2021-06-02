// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

#include "iree/base/internal/main.h"
#include "iree/base/logging.h"
#include "iree/base/target_platform.h"

#if defined(IREE_PLATFORM_WINDOWS)

#include <combaseapi.h>
#include <shellapi.h>

namespace iree {
namespace {

// Entry point when using /SUBSYSTEM:CONSOLE is the standard main().
extern "C" int main(int argc, char** argv) { return iree_main(argc, argv); }

// Entry point when using /SUBSYSTEM:WINDOWS.
extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Convert command line to an argv-like format.
  // NOTE: the command line that comes in with the WinMain arg is garbage.
  int argc = 0;
  wchar_t** argv_w = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (!argc || !argv_w) {
    IREE_LOG(FATAL) << "Unable to parse command line";
    return 1;
  }

  // Convert all args to narrow char strings.
  std::vector<std::string> allocated_strings(argc);
  std::vector<char*> argv_a(argc);
  for (int i = 0; i < argc; ++i) {
    size_t char_length = wcslen(argv_w[i]);
    allocated_strings[i].resize(char_length);
    argv_a[i] = const_cast<char*>(allocated_strings[i].data());
    std::wcstombs(argv_a[i], argv_w[i], char_length + 1);
  }
  ::LocalFree(argv_w);

  // Setup COM on the main thread.
  // NOTE: this may fail if COM has already been initialized - that's OK.
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

  // Run standard main function.
  int exit_code = iree_main(argc, argv_a.data());

  // Release arg memory.
  argv_a.clear();
  allocated_strings.clear();

  return exit_code;
}

}  // namespace
}  // namespace iree

#endif  // IREE_PLATFORM_WINDOWS
