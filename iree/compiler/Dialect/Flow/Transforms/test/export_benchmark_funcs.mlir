// RUN: iree-opt -split-input-file -iree-mhlo-input-transformation-pipeline -iree-flow-transformation-pipeline -iree-flow-export-benchmark-funcs %s | FileCheck %s

module {
  func.func @two_dispatch(%arg0: tensor<5x3xf32>, %arg1: tensor<3x5xf32>) -> (tensor<5x5xf32>, tensor<3x5xf32>) {
    %0 = "mhlo.dot"(%arg0, %arg1) : (tensor<5x3xf32>, tensor<3x5xf32>) -> tensor<5x5xf32>
    %1 = "mhlo.dot"(%arg1, %0) : (tensor<3x5xf32>, tensor<5x5xf32>) -> tensor<3x5xf32>
    return %0, %1 : tensor<5x5xf32>, tensor<3x5xf32>
  }
}

// CHECK-DAG: util.global private @[[MAIN_IN_0:.+]] {noinline} = dense<{{.*}}> : tensor<5x3xf32>
// CHECK-DAG: util.global private @[[MAIN_IN_1:.+]] {noinline} = dense<{{.*}}> : tensor<3x5xf32>
//     CHECK: func @two_dispatch_benchmark()
// CHECK-DAG: %[[ARG0:.+]] = util.global.load @[[MAIN_IN_0]] : tensor<5x3xf32>
// CHECK-DAG: %[[ARG1:.+]] = util.global.load @[[MAIN_IN_1]] : tensor<3x5xf32>
//     CHECK: %[[RET:.+]]:2 = call @two_dispatch(%[[ARG0]], %[[ARG1]])
// CHECK-DAG: util.do_not_optimize(%[[RET]]#0) : tensor<5x5xf32>
// CHECK-DAG: util.do_not_optimize(%[[RET]]#1) : tensor<3x5xf32>

// -----

func.func @while(%start: tensor<i32>, %bound: tensor<i32>) -> tensor<i32> {
  cf.br ^bb1(%start : tensor<i32>)
^bb1(%0: tensor<i32>):
  %1 = "mhlo.compare"(%0, %bound) {comparison_direction = #mhlo<"comparison_direction LT">} : (tensor<i32>, tensor<i32>) -> tensor<i1>
  %2 = tensor.extract %1[] : tensor<i1>
  cf.cond_br %2, ^bb2(%0 : tensor<i32>), ^bb3(%0 : tensor<i32>)
^bb2(%3: tensor<i32>):
  %4 = mhlo.add %3, %3 : tensor<i32>
  cf.br ^bb1(%4 : tensor<i32>)
^bb3(%5: tensor<i32>):
  return %5 : tensor<i32>
}

//     CHECK: util.global private @_benchmark_input_0 {noinline} = dense<0> : tensor<i32>
//     CHECK: util.global private @_benchmark_input_1 {noinline} = dense<0> : tensor<i32>
//     CHECK: func @while_benchmark() attributes {iree.abi.stub, iree.reflection = {iree.benchmark = "entry"}} {
// CHECK-DAG:   %[[ARG0:.+]] = util.global.load @_benchmark_input_0 : tensor<i32>
// CHECK-DAG:   %[[ARG1:.+]] = util.global.load @_benchmark_input_1 : tensor<i32>
//     CHECK:   %[[RET0:.+]] = call @while(%[[ARG0]], %[[ARG1]])
//     CHECK:   util.do_not_optimize(%[[RET0]]) : tensor<i32>
//     CHECK:   return
//     CHECK: }
