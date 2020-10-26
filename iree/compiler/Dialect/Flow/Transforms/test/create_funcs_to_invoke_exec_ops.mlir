// RUN: iree-opt -iree-flow-transformation-pipeline -iree-flow-export-dispatches %s | IreeFileCheck %s

module {
  func @two_dispatch(%arg0: tensor<5x3xf32>, %arg1: tensor<3x5xf32>) -> (tensor<5x5xf32>, tensor<3x5xf32>) attributes { iree.module.export } {
    %0 = "mhlo.dot"(%arg0, %arg1) : (tensor<5x3xf32>, tensor<3x5xf32>) -> tensor<5x5xf32>
    %1 = "mhlo.dot"(%arg1, %0) : (tensor<3x5xf32>, tensor<5x5xf32>) -> tensor<3x5xf32>
    return %0, %1 : tensor<5x5xf32>, tensor<3x5xf32>
  }
}
// CHECK: func @two_dispatch_ex_dispatch_0_entry
// CHECK: %{{.+}} = flow.variable.load {{.*}} : tensor<5x3xf32>
// CHECK: %{{.+}} = flow.variable.load {{.*}} : tensor<3x5xf32>
// CHECK: %[[RES:.+]] = flow.ex.stream.fragment({{.+}}) -> tensor<5x5xf32> {
// CHECK:   %[[DISPATCH_RES:.+]] = flow.dispatch @two_dispatch_ex_dispatch_0::@two_dispatch_ex_dispatch_0[%{{.+}} : index](%{{.+}}, %{{.+}}) : (tensor<5x3xf32>, tensor<3x5xf32>) -> tensor<5x5xf32>
// CHECK:   flow.return %[[DISPATCH_RES]] : tensor<5x5xf32>
// CHECK: return %[[RES]] : tensor<5x5xf32>
//
// CHECK: func @two_dispatch_ex_dispatch_1_entry
// CHECK: %[[ARG0:.+]] = flow.variable.load {{.*}} : tensor<3x5xf32>
// CHECK: %[[ARG1:.+]] = flow.variable.load {{.*}} : tensor<5x5xf32>
// CHECK: %[[RES:.+]] = flow.ex.stream.fragment({{.+}}) -> tensor<3x5xf32>
// CHECK:   %[[DISPATCH_RES:.+]] = flow.dispatch @two_dispatch_ex_dispatch_1::@two_dispatch_ex_dispatch_1[%{{.+}} : index](%{{.+}}, %{{.+}}) : (tensor<3x5xf32>, tensor<5x5xf32>) -> tensor<3x5xf32>
// CHECK:   flow.return %[[DISPATCH_RES]] : tensor<3x5xf32>
// CHECK: return %[[RES]] : tensor<3x5xf32>
