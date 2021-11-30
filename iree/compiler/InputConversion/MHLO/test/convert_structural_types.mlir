// RUN: iree-opt -split-input-file --iree-mhlo-to-linalg-on-tensors %s | IreeFileCheck %s

// CHECK-LABEL: @func_cfg_conversion
module @func_cfg_conversion {
  // CHECK: func @caller(%arg0: tensor<2xi32>, %arg1: i1) -> tensor<2xi32>
  func @caller(%arg0: tensor<2xui32>, %arg1 : i1) -> tensor<2xui32> {
    // CHECK: %[[RESULT:.*]] = call @callee(%arg0, %arg1) : (tensor<2xi32>, i1) -> tensor<2xi32>
    %1 = call @callee(%arg0, %arg1) : (tensor<2xui32>, i1) -> tensor<2xui32>
    // CHECK: return %[[RESULT]] : tensor<2xi32>
    return %1 : tensor<2xui32>
  }

  // CHECK: func @callee(%arg0: tensor<2xi32>, %arg1: i1) -> tensor<2xi32>
  func @callee(%arg0: tensor<2xui32>, %arg1: i1) -> tensor<2xui32> {
    // CHECK: cond_br %arg1, ^bb1(%arg0 : tensor<2xi32>), ^bb2(%arg0 : tensor<2xi32>)
    cond_br %arg1, ^bb1(%arg0 : tensor<2xui32>), ^bb2(%arg0 : tensor<2xui32>)
  // CHECK: ^bb1(%[[BB1_PHI:.*]]: tensor<2xi32>)
  ^bb1(%phi0 : tensor<2xui32>) :
    // CHECK: br ^bb2(%[[BB1_PHI]] : tensor<2xi32>)
    br ^bb2(%phi0 : tensor<2xui32>)
  // CHECK: ^bb2(%[[BB2_PHI:.*]]: tensor<2xi32>)
  ^bb2(%phi1 : tensor<2xui32>):
    return %phi1 : tensor<2xui32>
  }
}
