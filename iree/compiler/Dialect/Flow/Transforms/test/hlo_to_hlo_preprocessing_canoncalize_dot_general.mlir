// RUN: iree-opt -split-input-file -verify-diagnostics -iree-flow-hlo-to-hlo-preprocessing %s | IreeFileCheck %s

func @dot_general_to_dot(%arg0: tensor<1x32x128x4xf32>, %arg1: tensor<128x4x8x64xf32>) -> tensor<1x32x8x64xf32> {
  %0 = "mhlo.dot_general"(%arg0, %arg1) {
      dot_dimension_numbers = {
        lhs_batching_dimensions = dense<> : tensor<0xi64>,
        lhs_contracting_dimensions = dense<[2, 3]> : tensor<2xi64>,
        rhs_batching_dimensions = dense<> : tensor<0xi64>,
        rhs_contracting_dimensions = dense<[0, 1]> : tensor<2xi64>
      }, name = "dot_general_to_dot", precision_config = ["DEFAULT", "DEFAULT"]
    } : (tensor<1x32x128x4xf32>, tensor<128x4x8x64xf32>) -> tensor<1x32x8x64xf32>
  return %0 : tensor<1x32x8x64xf32>
}

// CHECK: dot_general_to_dot(%[[ARG0:.+]]: tensor<1x32x128x4xf32>, %[[ARG1:.+]]: tensor<128x4x8x64xf32>) -> tensor<1x32x8x64xf32>
// CHECK: %[[ARG0_RESHAPED:.+]] = "mhlo.reshape"(%[[ARG0]]) : (tensor<1x32x128x4xf32>) -> tensor<32x512xf32>
// CHECK: %[[ARG1_RESHAPED:.+]] = "mhlo.reshape"(%[[ARG1]]) : (tensor<128x4x8x64xf32>) -> tensor<512x512xf32>
// CHECK: %[[DOT:.+]] = "mhlo.dot"(%[[ARG0_RESHAPED]], %[[ARG1_RESHAPED]])
// CHECK: %[[RESULT:.+]] = "mhlo.reshape"(%[[DOT]]) : (tensor<32x512xf32>) -> tensor<1x32x8x64xf32>
// CHECK: return %[[RESULT]] : tensor<1x32x8x64xf32>

// -----

func @dot_general_to_dot_general_rank_reduced(%arg0: tensor<1x8x32x64xf32>, %arg1 : tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32> {
  %0 = "mhlo.dot_general"(%arg0, %arg1) {
    dot_dimension_numbers = {
      lhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      lhs_contracting_dimensions = dense<3> : tensor<1xi64>,
      rhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      rhs_contracting_dimensions = dense<2> : tensor<1xi64>
    }, name = "dot_general_to_dot", precision_config = ["DEFAULT", "DEFAULT"]
  } : (tensor<1x8x32x64xf32>, tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32>
  return %0 : tensor<1x8x32x32xf32>
}
// CHECK: dot_general_to_dot_general_rank_reduced(%[[ARG0:.+]]: tensor<1x8x32x64xf32>, %[[ARG1:.+]]: tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32>
// CHECK: %[[ARG0_RESHAPED:.+]] = "mhlo.reshape"(%[[ARG0]]) : (tensor<1x8x32x64xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[ARG1_RESHAPED:.+]] = "mhlo.reshape"(%[[ARG1]]) : (tensor<1x8x64x32xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[DOT_RESULT:.+]] = "mhlo.dot_general"(%[[ARG0_RESHAPED]], %[[ARG1_RESHAPED]])
// CHECK: %[[RESULT:.+]] = "mhlo.reshape"(%[[DOT_RESULT]]) : (tensor<8x32x32xf32>) -> tensor<1x8x32x32xf32>
// CHECK: return %[[RESULT]] : tensor<1x8x32x32xf32>

// -----

func @dot_general_to_dot_general_rank_reduced_a_transposed(%arg0: tensor<1x8x64x32xf32>, %arg1: tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32> {
  %0 = "mhlo.dot_general"(%arg0, %arg1) {
    dot_dimension_numbers = {
      lhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      lhs_contracting_dimensions = dense<2> : tensor<1xi64>,
      rhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      rhs_contracting_dimensions = dense<2> : tensor<1xi64>
    }, name = "dot_general_to_dot_trans_a", precision_config = ["DEFAULT", "DEFAULT"]
  } : (tensor<1x8x64x32xf32>, tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32>
  return %0 : tensor<1x8x32x32xf32>
}
// CHECK: dot_general_to_dot_general_rank_reduced_a_transposed(%[[ARG0:.+]]: tensor<1x8x64x32xf32>, %[[ARG1:.+]]: tensor<1x8x64x32xf32>) -> tensor<1x8x32x32xf32>
// CHECK: %[[ARG0_RESHAPED:.+]] = "mhlo.reshape"(%[[ARG0]]) : (tensor<1x8x64x32xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[ARG1_RSSHAPED:.+]] = "mhlo.reshape"(%[[ARG1]]) : (tensor<1x8x64x32xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[ARG0_RESHAPED_TR:.+]] = "mhlo.transpose"(%[[ARG0_RESHAPED]]) {permutation = dense<[0, 2, 1]> : tensor<3xi64>} : (tensor<8x64x32xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[DOT_RESULT:.+]] = "mhlo.dot_general"(%[[ARG0_RESHAPED_TR]], %[[ARG1_RSSHAPED]])
// CHECK: %[[RESULT:.+]] = "mhlo.reshape"(%[[DOT_RESULT]]) : (tensor<8x32x32xf32>) -> tensor<1x8x32x32xf32>

// -----

func @dot_general_to_dot_general_rank_reduced_b_transposed(%arg0: tensor<1x8x32x64xf32>, %arg1: tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32> {
  %0 = "mhlo.dot_general"(%arg0, %arg1) {
    dot_dimension_numbers = {
      lhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      lhs_contracting_dimensions = dense<3> : tensor<1xi64>,
      rhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      rhs_contracting_dimensions = dense<3> : tensor<1xi64>
    }, name = "dot_general_to_dot_trans_b", precision_config = ["DEFAULT", "DEFAULT"]
  } : (tensor<1x8x32x64xf32>, tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32>
  return %0 : tensor<1x8x32x32xf32>
}
// CHECK: dot_general_to_dot_general_rank_reduced_b_transposed(%[[ARG0:.+]]: tensor<1x8x32x64xf32>, %[[ARG1:.+]]: tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32>
// CHECK: %[[ARG0_REHSPAED:.+]] = "mhlo.reshape"(%[[ARG0]]) : (tensor<1x8x32x64xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[ARG1_REHSPAED:.+]] = "mhlo.reshape"(%[[ARG1]]) : (tensor<1x8x32x64xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[ARG1_REHSPAED_TR:.+]] = "mhlo.transpose"(%[[ARG1_REHSPAED]]) {permutation = dense<[0, 2, 1]> : tensor<3xi64>} : (tensor<8x32x64xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[DOT_RESULT:.+]] = "mhlo.dot_general"(%[[ARG0_REHSPAED]], %[[ARG1_REHSPAED_TR]])
// CHECK: %[[RESULT:.+]] = "mhlo.reshape"(%[[DOT_RESULT]]) : (tensor<8x32x32xf32>) -> tensor<1x8x32x32xf32>
// CHECK: return %[[RESULT]] : tensor<1x8x32x32xf32>


// -----

func @dot_general_to_dot_general_rank_reduced_ab_transposed(%arg0: tensor<1x8x64x32xf32>, %arg1: tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32> {
  %0 = "mhlo.dot_general"(%arg0, %arg1) {
    dot_dimension_numbers = {
      lhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      lhs_contracting_dimensions = dense<2> : tensor<1xi64>,
      rhs_batching_dimensions = dense<[0, 1]> : tensor<2xi64>,
      rhs_contracting_dimensions = dense<3> : tensor<1xi64>
    }, name = "dot_general_to_dot_trans_ab", precision_config = ["DEFAULT", "DEFAULT"]
  } : (tensor<1x8x64x32xf32>, tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32>
  return %0 : tensor<1x8x32x32xf32>
}
// CHECK: dot_general_to_dot_general_rank_reduced_ab_transposed(%[[ARG0:.+]]: tensor<1x8x64x32xf32>, %[[ARG1:.+]]: tensor<1x8x32x64xf32>) -> tensor<1x8x32x32xf32>
// CHECK: %[[ARG0_REHSPAED:.+]] = "mhlo.reshape"(%[[ARG0]]) : (tensor<1x8x64x32xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[ARG1_REHSPAED:.+]] = "mhlo.reshape"(%[[ARG1]]) : (tensor<1x8x32x64xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[ARG0_REHSPAED_TR:.+]] = "mhlo.transpose"(%[[ARG0_REHSPAED]]) {permutation = dense<[0, 2, 1]> : tensor<3xi64>} : (tensor<8x64x32xf32>) -> tensor<8x32x64xf32>
// CHECK: %[[ARG1_REHSPAED_TR:.+]] = "mhlo.transpose"(%[[ARG1_REHSPAED]]) {permutation = dense<[0, 2, 1]> : tensor<3xi64>} : (tensor<8x32x64xf32>) -> tensor<8x64x32xf32>
// CHECK: %[[DOT_RESULT:.+]] = "mhlo.dot_general"(%[[ARG0_REHSPAED_TR]], %[[ARG1_REHSPAED_TR]])
// CHECK: %[[RESULT:.+]] = "mhlo.reshape"(%[[DOT_RESULT]]) : (tensor<8x32x32xf32>) -> tensor<1x8x32x32xf32>
// CHECK: return %[[RESULT]] : tensor<1x8x32x32xf32>
