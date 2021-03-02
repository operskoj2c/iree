func @if_true_test() attributes { iree.module.export } {
  %0 = iree.unfoldable_constant dense<true> : tensor<i1>
  %1 = iree.unfoldable_constant dense<10> : tensor<i32>
  %path = iree.unfoldable_constant 1 : i32
  %2 = "tosa.cond_if"(%0, %1) ( {
  ^bb0(%arg0 : tensor<i32>):
    check.expect_true(%path) : i32
    %3 = iree.unfoldable_constant dense<10> : tensor<i32>
    %4 = "tosa.add"(%arg0, %3) : (tensor<i32>, tensor<i32>) -> tensor<i32>
    "tosa.yield"(%4) : (tensor<i32>) -> ()
  },  {
  ^bb0(%arg0 : tensor<i32>):
    check.expect_false(%path) : i32
    "tosa.yield"(%arg0) : (tensor<i32>) -> ()
  }) : (tensor<i1>, tensor<i32>) -> (tensor<i32>)
  check.expect_eq_const(%2, dense<20> : tensor<i32>) : tensor<i32>
  return
}

func @if_false_test() attributes { iree.module.export } {
  %0 = iree.unfoldable_constant dense<false> : tensor<i1>
  %1 = iree.unfoldable_constant dense<10> : tensor<i32>
  %path = iree.unfoldable_constant 0 : i32
  %2 = "tosa.cond_if"(%0, %1) ( {
  ^bb0(%arg0 : tensor<i32>):
    check.expect_true(%path) : i32
    "tosa.yield"(%arg0) : (tensor<i32>) -> ()
  },  {
  ^bb0(%arg0 : tensor<i32>):
    check.expect_false(%path) : i32
    %3 = iree.unfoldable_constant dense<10> : tensor<i32>
    %4 = "tosa.add"(%arg0, %3) : (tensor<i32>, tensor<i32>) -> tensor<i32>
    "tosa.yield"(%4) : (tensor<i32>) -> ()
  }) : (tensor<i1>, tensor<i32>) -> (tensor<i32>)
  check.expect_eq_const(%2, dense<20> : tensor<i32>) : tensor<i32>
  return
}

