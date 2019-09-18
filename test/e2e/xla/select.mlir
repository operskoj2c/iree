// RUN: iree-run-mlir --target_backends=interpreter-bytecode %s --input_values="4xi8=[1 0 200 0]" | FileCheck %s
// CHECK-LABEL: EXEC @select
func @select(%cond : tensor<4xi1>) -> tensor<4xf32> {
  %lhs = constant dense<[1.0, 2.0, 3.0, 4.0]> : tensor<4xf32>
  %rhs = constant dense<[5.0, 6.0, 7.0, 8.0]> : tensor<4xf32>
  %result = "xla_hlo.select"(%cond, %lhs, %rhs) : (tensor<4xi1>, tensor<4xf32>, tensor<4xf32>) -> tensor<4xf32>
  return %result : tensor<4xf32>
}
// CHECK: 4xf32=1 6 3 8
