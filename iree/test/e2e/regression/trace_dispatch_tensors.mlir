// RUN: iree-run-mlir --iree-input-type=mhlo -iree-hal-target-backends=vmvx -iree-flow-trace-dispatch-tensors2 %s 2>&1 | FileCheck %s

func @two_dispatch() -> (tensor<5x5xf32>, tensor<3x5xf32>) {
  %0 = util.unfoldable_constant dense<1.0> : tensor<5x3xf32>
  %1 = util.unfoldable_constant dense<0.4> : tensor<3x5xf32>
  %2 = "mhlo.dot"(%0, %1) : (tensor<5x3xf32>, tensor<3x5xf32>) -> tensor<5x5xf32>
  %3 = "mhlo.dot"(%1, %2) : (tensor<3x5xf32>, tensor<5x5xf32>) -> tensor<3x5xf32>
  return %2, %3 : tensor<5x5xf32>, tensor<3x5xf32>
}
// CHECK: === two_dispatch_dispatch_0::two_dispatch_dispatch_0 inputs ===
// CHECK: 5x3xf32=
// CHECK: 3x5xf32=
// CHECK: === two_dispatch_dispatch_0::two_dispatch_dispatch_0 outputs ===
// CHECK: 5x5xf32=
// CHECK: === two_dispatch_dispatch_1::two_dispatch_dispatch_1 inputs ===
// CHECK: 3x5xf32=
// CHECK: 5x5xf32=
// CHECK: === two_dispatch_dispatch_1::two_dispatch_dispatch_1 outputs ===
// CHECK: 3x5xf32=
