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

// RUN: iree-opt -split-input-file -iree-index-computation -simplify-spirv-affine-exprs=false -convert-iree-to-spirv -verify-diagnostics -o - %s | IreeFileCheck %s

module {
  // CHECK-DAG: spv.globalVariable [[GLOBALIDVAR:@.*]] built_in("GlobalInvocationId") : !spv.ptr<vector<3xi32>, Input>
  // CHECK: func @mul_1D
  // CHECK-SAME: [[ARG0:%[a-zA-Z0-9]*]]: !spv.ptr<!spv.struct<!spv.array<4 x f32 [4]> [0]>, StorageBuffer>
  // CHECK-SAME: [[ARG1:%[a-zA-Z0-9]*]]: !spv.ptr<!spv.struct<!spv.array<4 x f32 [4]> [0]>, StorageBuffer>
  // CHECK-SAME: [[ARG2:%[a-zA-Z0-9]*]]: !spv.ptr<!spv.struct<!spv.array<4 x f32 [4]> [0]>, StorageBuffer>
  func @mul_1D(%arg0: memref<4xf32>, %arg1: memref<4xf32>, %arg2: memref<4xf32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[4, 1, 1]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: [[GLOBALIDPTR:%.*]] = spv._address_of [[GLOBALIDVAR]]
    // CHECK: [[GLOBALID:%.*]] = spv.Load "Input" [[GLOBALIDPTR]]
    // CHECK: [[GLOBALIDX:%.*]] = spv.CompositeExtract [[GLOBALID]][0 : i32]
    // CHECK: spv.selection
    // CHECK: [[ZERO1:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG0LOADPTR:%.*]] = spv.AccessChain [[ARG0]]{{\[}}[[ZERO1]], [[GLOBALIDX]]{{\]}}
    // CHECK: [[VAL1:%.*]] = spv.Load "StorageBuffer" [[ARG0LOADPTR]]
    %0 = iree.load_input(%arg0 : memref<4xf32>) : tensor<4xf32>
    // CHECK: [[ZERO2:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG1LOADPTR:%.*]] = spv.AccessChain [[ARG1]]{{\[}}[[ZERO2]], [[GLOBALIDX]]{{\]}}
    // CHECK: [[VAL2:%.*]] = spv.Load "StorageBuffer" [[ARG1LOADPTR]]
    %1 = iree.load_input(%arg1 : memref<4xf32>) : tensor<4xf32>
    // CHECK: [[RESULT:%.*]] = spv.FMul [[VAL1]], [[VAL2]]
    %2 = mulf %0, %1 : tensor<4xf32>
    // CHECK: [[ZERO3:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG2STOREPTR:%.*]] = spv.AccessChain [[ARG2]]{{\[}}[[ZERO3]], [[GLOBALIDX]]{{\]}}
    // CHECK: spv.Store "StorageBuffer" [[ARG2STOREPTR]], [[RESULT]]
    iree.store_output(%2 : tensor<4xf32>, %arg2 : memref<4xf32>)
    iree.return
  }
}
