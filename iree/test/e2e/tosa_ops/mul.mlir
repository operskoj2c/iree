func @tensor_float() {
  %0 = iree.unfoldable_constant dense<[1.0, 0.0, 3.0, 4.0]> : tensor<4xf32>
  %1 = iree.unfoldable_constant dense<[5.0, 6.0, -3.0, 8.0]> : tensor<4xf32>
  %result = "tosa.mul"(%0, %1) {shift = 0 : i32} : (tensor<4xf32>, tensor<4xf32>) -> tensor<4xf32>
  check.expect_almost_eq_const(%result, dense<[5.0, 0.0, -9.0, 32.0]> : tensor<4xf32>) : tensor<4xf32>
  return
}

func @tensor_int() {
  %0 = iree.unfoldable_constant dense<[1, 0, 3, 4]> : tensor<4xi32>
  %1 = iree.unfoldable_constant dense<[5, 6, -3, 8]> : tensor<4xi32>
  %result = "tosa.mul"(%0, %1) {shift = 0 : i32} : (tensor<4xi32>, tensor<4xi32>) -> tensor<4xi32>
  check.expect_eq_const(%result, dense<[5, 0, -9, 32]> : tensor<4xi32>) : tensor<4xi32>
  return
}

func @tensor_int_shifted() {
  %0 = iree.unfoldable_constant dense<[1, 0, 3, 4]> : tensor<4xi32>
  %1 = iree.unfoldable_constant dense<[5, 6, -3, 8]> : tensor<4xi32>
  %result = "tosa.mul"(%0, %1) {shift = 1 : i32} : (tensor<4xi32>, tensor<4xi32>) -> tensor<4xi32>
  check.expect_eq_const(%result, dense<[3, 0, -4, 16]> : tensor<4xi32>) : tensor<4xi32>
  return
}

