func @reduce_window_nonoverlapping_1x4x6x1xf32() {
  %0 = util.unfoldable_constant dense<[[[[ 1.0], [ 2.0], [ 3.0], [ 4.0], [ 5.0], [ 6.0]],
                                        [[ 7.0], [ 8.0], [ 9.0], [10.0], [11.0], [12.0]],
                                        [[13.0], [14.0], [15.0], [16.0], [17.0], [18.0]],
                                        [[19.0], [20.0], [21.0], [22.0], [23.0], [24.0]]]]> : tensor<1x4x6x1xf32>
  %1 = util.unfoldable_constant dense<0.0> : tensor<f32>
  %res = "mhlo.reduce_window"(%0, %1) ( {
  ^bb0(%arg0: tensor<f32>, %arg1: tensor<f32>):   // no predecessors
    %3 = "mhlo.add"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
    "mhlo.return"(%3) : (tensor<f32>) -> ()
  }) {window_dimensions = dense<[1, 2, 3, 1]> : tensor<4xi64>,
      window_strides = dense<[1, 2, 3, 1]> : tensor<4xi64>} : (tensor<1x4x6x1xf32>, tensor<f32>) -> tensor<1x2x2x1xf32>
  check.expect_eq_const(%res, dense<[[[[30.0], [48.0]],[[102.0], [120.0]]]]> : tensor<1x2x2x1xf32>) : tensor<1x2x2x1xf32>
  return
}

func @reduce_window_overlapping_4x6xf32() {
  %0 = util.unfoldable_constant dense<[[[[ 1.0], [ 2.0], [ 3.0], [ 4.0], [ 5.0], [ 6.0]],
                                        [[ 7.0], [ 8.0], [ 9.0], [10.0], [11.0], [12.0]],
                                        [[13.0], [14.0], [15.0], [16.0], [17.0], [18.0]],
                                        [[19.0], [20.0], [21.0], [22.0], [23.0], [24.0]]]]> : tensor<1x4x6x1xf32>
  %1 = util.unfoldable_constant dense<0.0> : tensor<f32>
  %res = "mhlo.reduce_window"(%0, %1) ( {
  ^bb0(%arg0: tensor<f32>, %arg1: tensor<f32>):   // no predecessors
    %3 = "mhlo.add"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
    "mhlo.return"(%3) : (tensor<f32>) -> ()
  }) {window_dimensions = dense<[1, 2, 3, 1]> : tensor<4xi64>,
      window_strides = dense<[1, 1, 1, 1]> : tensor<4xi64>} : (tensor<1x4x6x1xf32>, tensor<f32>) -> tensor<1x3x4x1xf32>
  check.expect_eq_const(%res, dense<[[
      [[ 30.0], [ 36.0], [ 42.0], [ 48.0]],
      [[ 66.0], [ 72.0], [ 78.0], [ 84.0]],
      [[102.0], [108.0], [114.0], [120.0]]]]> : tensor<1x3x4x1xf32>) : tensor<1x3x4x1xf32>
  return
}

func @reduce_window_max_4x6xf32() {
  %0 = util.unfoldable_constant dense<[[[[ 1.0], [ 2.0], [ 3.0], [ 4.0], [ 5.0], [ 6.0]],
                                        [[ 7.0], [ 8.0], [ 9.0], [10.0], [11.0], [12.0]],
                                        [[13.0], [14.0], [15.0], [16.0], [17.0], [18.0]],
                                        [[19.0], [20.0], [21.0], [22.0], [23.0], [24.0]]]]> : tensor<1x4x6x1xf32>
  %1 = util.unfoldable_constant dense<0.0> : tensor<f32>
  %res = "mhlo.reduce_window"(%0, %1) ( {
  ^bb0(%arg0: tensor<f32>, %arg1: tensor<f32>):   // no predecessors
    %3 = "mhlo.maximum"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
    "mhlo.return"(%3) : (tensor<f32>) -> ()
  }) {window_dimensions = dense<[1, 2, 3, 1]> : tensor<4xi64>,
      window_strides = dense<[1, 2, 3, 1]> : tensor<4xi64>} : (tensor<1x4x6x1xf32>, tensor<f32>) -> tensor<1x2x2x1xf32>
  check.expect_almost_eq_const(%res, dense<[[[[9.0], [12.0]], [[21.0], [24.0]]]]> : tensor<1x2x2x1xf32>) : tensor<1x2x2x1xf32>
  return
}

func @reduce_window_min_4x6xf32() {
  %0 = util.unfoldable_constant dense<[[[[ 1.0], [ 2.0], [ 3.0], [ 4.0], [ 5.0], [ 6.0]],
                                        [[ 7.0], [ 8.0], [ 9.0], [10.0], [11.0], [12.0]],
                                        [[13.0], [14.0], [15.0], [16.0], [17.0], [18.0]],
                                        [[19.0], [20.0], [21.0], [22.0], [23.0], [24.0]]]]> : tensor<1x4x6x1xf32>
  %1 = util.unfoldable_constant dense<14.0> : tensor<f32>
  %res = "mhlo.reduce_window"(%0, %1) ( {
  ^bb0(%arg0: tensor<f32>, %arg1: tensor<f32>):   // no predecessors
    %3 = "mhlo.minimum"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
    "mhlo.return"(%3) : (tensor<f32>) -> ()
  }) {window_dimensions = dense<[1, 2, 3, 1]> : tensor<4xi64>,
      window_strides = dense<[1, 2, 3, 1]> : tensor<4xi64>} : (tensor<1x4x6x1xf32>, tensor<f32>) -> tensor<1x2x2x1xf32>
  check.expect_almost_eq_const(%res, dense<[[[[1.0], [4.0]], [[13.0], [14.0]]]]> : tensor<1x2x2x1xf32>) : tensor<1x2x2x1xf32>
  return
}
