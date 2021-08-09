func @dynamic_update_slice_2x2() {
  %target = util.unfoldable_constant dense<2> : tensor<3x3xi32>
  %update = util.unfoldable_constant dense<1> : tensor<2x2xi32>
  %c0 = util.unfoldable_constant dense<0> : tensor<i32>
  %result = "mhlo.dynamic-update-slice"(%target, %update, %c0, %c0)
    : (tensor<3x3xi32>, tensor<2x2xi32>, tensor<i32>, tensor<i32>) -> tensor<3x3xi32>
  check.expect_eq_const(%result, dense<[
    [1, 1, 2],
    [1, 1, 2],
    [2, 2, 2]]> : tensor<3x3xi32>) : tensor<3x3xi32>
  return
}

func @dynamic_update_slice_1x3() {
  %target = util.unfoldable_constant dense<2> : tensor<3x3xi32>
  %update = util.unfoldable_constant dense<1> : tensor<1x3xi32>
  %c0 = util.unfoldable_constant dense<0> : tensor<i32>
  %c1 = util.unfoldable_constant dense<1> : tensor<i32>
  %result = "mhlo.dynamic-update-slice"(%target, %update, %c1, %c0)
    : (tensor<3x3xi32>, tensor<1x3xi32>, tensor<i32>, tensor<i32>) -> tensor<3x3xi32>
  check.expect_eq_const(%result, dense<[
    [2, 2, 2],
    [1, 1, 1],
    [2, 2, 2]]> : tensor<3x3xi32>) : tensor<3x3xi32>
  return
}

func @into_constant() {
  %update = util.unfoldable_constant dense<2> : tensor<1xi32>
  %target = mhlo.constant dense<1> : tensor<4xi32>
  %index = mhlo.constant dense<0> : tensor<i32>
  %result = "mhlo.dynamic-update-slice"(%target, %update, %index) : (tensor<4xi32>, tensor<1xi32>, tensor<i32>) -> tensor<4xi32>
  check.expect_eq_const(%result, dense<[2, 1, 1, 1]> : tensor<4xi32>) : tensor<4xi32>
  return
}
