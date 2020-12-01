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

#include "benchmark/benchmark.h"
#include "iree/base/flags.h"

namespace iree {

extern "C" int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  iree_flags_parse_checked(&argc, &argv);
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}

}  // namespace iree
