// RUN: iree-opt -split-input-file -iree-codegen-vectorize-linalg-conv -canonicalize -cse %s | IreeFileCheck %s

func @vectorize_conv(%filter: memref<1x1x4x4xf32>, %input: memref<1x1x7x4xf32>, %output: memref<1x1x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [1, 1, 7, 4] [1, 1, 1, 1]  : memref<1x1x7x4xf32> to memref<1x1x7x4xf32>
  %2 = subview %output[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [2, 2]} : memref<1x1x4x4xf32>, memref<1x1x7x4xf32>, memref<1x1x4x4xf32>
  return
}

// CHECK: #map0 = affine_map<(d0, d1, d2) -> (d0, d2)>
// CHECK: #map1 = affine_map<(d0, d1, d2) -> (d2, d1)>
// CHECK: #map2 = affine_map<(d0, d1, d2) -> (d0, d1)>

// CHECK: func @vectorize_conv
// CHECK-SAME: %[[FILTER_ARG:.+]]: memref<1x1x4x4xf32>,
// CHECK-SAME: %[[INPUT_ARG:.+]]: memref<1x1x7x4xf32>,
// CHECK-SAME: %[[OUTPUT_ARG:.+]]: memref<1x1x4x4xf32>

// CHECK: %[[FLOAT_ZERO:.+]] = constant 0.000000e+00 : f32
// CHECK: %[[FILTER:.+]] = subview %[[FILTER_ARG]]
// CHECK: %[[INPUT:.+]] = subview %[[INPUT_ARG]]
// CHECK: %[[OUTPUT:.+]] = subview %[[OUTPUT_ARG]]

// Read in the filter and get slices
// CHECK: %[[FILTER_VECTOR:.+]] = vector.transfer_read %[[FILTER]][%c0, %c0, %c0, %c0], %cst {masked = [false, false]} : memref<1x1x4x4xf32>, vector<4x4xf32>
// CHECK: %[[FILTER_0:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [0, 0], sizes = [1, 4], strides = [1, 1]} : vector<4x4xf32> to vector<1x4xf32>
// CHECK: %[[FILTER_1:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [1, 0], sizes = [1, 4], strides = [1, 1]} : vector<4x4xf32> to vector<1x4xf32>
// CHECK: %[[FILTER_2:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [2, 0], sizes = [1, 4], strides = [1, 1]} : vector<4x4xf32> to vector<1x4xf32>
// CHECK: %[[FILTER_3:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [3, 0], sizes = [1, 4], strides = [1, 1]} : vector<4x4xf32> to vector<1x4xf32>

// Handle batch #0
// CHECK: %[[INPUT_0:.+]] = vector.transfer_read %[[INPUT]][%c0, %c0, %c0, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x7x4xf32>, vector<1x4xf32>
// CHECK: %[[OUTPUT_0:.+]] = vector.transfer_read %[[OUTPUT]][%c0, %c0, %c0, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x4x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_0_0:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_0_0]], %[[FILTER_0]], %[[OUTPUT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_0_1:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_0_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_0_2:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_0_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_0_3:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 3], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_3:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_0_3]], %[[FILTER_3]], %[[DOT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_3]], %[[OUTPUT]][%c0, %c0, %c0, %c0] {masked = [false, false]} : vector<1x4xf32>, memref<1x1x4x4xf32>

// Handle batch #1
// CHECK: %[[INPUT_1:.+]] = vector.transfer_read %[[INPUT]][%c0, %c0, %c2, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x7x4xf32>, vector<1x4xf32>
// CHECK: %[[OUTPUT_1:.+]] = vector.transfer_read %[[OUTPUT]][%c0, %c0, %c1, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x4x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_1_0:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_1_0]], %[[FILTER_0]], %[[OUTPUT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_1_1:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_1_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_1_2:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_1_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_1_3:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 3], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_3:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_1_3]], %[[FILTER_3]], %[[DOT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_3]], %[[OUTPUT]][%c0, %c0, %c1, %c0] {masked = [false, false]} : vector<1x4xf32>, memref<1x1x4x4xf32>

// Handle batch #2
// CHECK: %[[INPUT_2:.+]] = vector.transfer_read %[[INPUT]][%c0, %c0, %c4, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x7x4xf32>, vector<1x4xf32>
// CHECK: %[[OUTPUT_2:.+]] = vector.transfer_read %[[OUTPUT]][%c0, %c0, %c2, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x4x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_2_0:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_2_0]], %[[FILTER_0]], %[[OUTPUT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_2_1:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_2_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_2_2:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_2_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_2_3:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 3], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_3:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_2_3]], %[[FILTER_3]], %[[DOT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_3]], %[[OUTPUT]][%c0, %c0, %c2, %c0] {masked = [false, false]} : vector<1x4xf32>, memref<1x1x4x4xf32>

// Handle batch #3
// CHECK: %[[INPUT_3:.+]] = vector.transfer_read %[[INPUT]][%c0, %c0, %c6, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x7x4xf32>, vector<1x4xf32>
// CHECK: %[[OUTPUT_3:.+]] = vector.transfer_read %[[OUTPUT]][%c0, %c0, %c3, %c0], %[[FLOAT_ZERO]] {masked = [false, false]} : memref<1x1x4x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_3_0:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_3_0]], %[[FILTER_0]], %[[OUTPUT_3]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_3_1:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_3_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_3_2:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_3_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_3_3:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 3], sizes = [1, 1], strides = [1, 1]} : vector<1x4xf32> to vector<1x1xf32>
// CHECK: %[[DOT_3:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"]} %[[INPUT_3_3]], %[[FILTER_3]], %[[DOT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_3]], %[[OUTPUT]][%c0, %c0, %c3, %c0] {masked = [false, false]} : vector<1x4xf32>, memref<1x1x4x4xf32>

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_batch
func @do_not_vectorize_conv_with_non_1_batch(%filter: memref<1x1x4x4xf32>, %input: memref<2x1x7x4xf32>, %output: memref<2x1x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [2, 1, 7, 4] [1, 1, 1, 1]  : memref<2x1x7x4xf32> to memref<2x1x7x4xf32>
  %2 = subview %output[0, 0, 0, 0] [2, 1, 4, 4] [1, 1, 1, 1]  : memref<2x1x4x4xf32> to memref<2x1x4x4xf32>
  // CHECK: linalg.conv
  linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [2, 2]} : memref<1x1x4x4xf32>, memref<2x1x7x4xf32>, memref<2x1x4x4xf32>
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_output_height
func @do_not_vectorize_conv_with_non_1_output_height(%filter: memref<1x1x4x4xf32>, %input: memref<1x3x7x4xf32>, %output: memref<1x2x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [1, 3, 7, 4] [1, 1, 1, 1]  : memref<1x3x7x4xf32> to memref<1x3x7x4xf32>
  %2 = subview %output[0, 0, 0, 0] [1, 2, 4, 4] [1, 1, 1, 1]  : memref<1x2x4x4xf32> to memref<1x2x4x4xf32>
  // CHECK: linalg.conv
  linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [2, 2]} : memref<1x1x4x4xf32>, memref<1x3x7x4xf32>, memref<1x2x4x4xf32>
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_filter_height
func @do_not_vectorize_conv_with_non_1_filter_height(%filter: memref<2x1x4x4xf32>, %input: memref<1x2x7x4xf32>, %output: memref<1x1x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [2, 1, 4, 4] [1, 1, 1, 1]  : memref<2x1x4x4xf32> to memref<2x1x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [1, 2, 7, 4] [1, 1, 1, 1]  : memref<1x2x7x4xf32> to memref<1x2x7x4xf32>
  %2 = subview %output[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  // CHECK: linalg.conv
  linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [2, 2]} : memref<2x1x4x4xf32>, memref<1x2x7x4xf32>, memref<1x1x4x4xf32>
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_filter_width
func @do_not_vectorize_conv_with_non_1_filter_width(%filter: memref<1x2x4x4xf32>, %input: memref<1x1x8x4xf32>, %output: memref<1x1x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [1, 2, 4, 4] [1, 1, 1, 1]  : memref<1x2x4x4xf32> to memref<1x2x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [1, 1, 8, 4] [1, 1, 1, 1]  : memref<1x1x8x4xf32> to memref<1x1x8x4xf32>
  %2 = subview %output[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  // CHECK: linalg.conv
  linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [2, 2]} : memref<1x2x4x4xf32>, memref<1x1x8x4xf32>, memref<1x1x4x4xf32>
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_dilation
func @do_not_vectorize_conv_with_non_1_dilation(%filter: memref<1x1x4x4xf32>, %input: memref<1x1x7x4xf32>, %output: memref<1x1x4x4xf32>) {
  %0 = subview %filter[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  %1 = subview %input[0, 0, 0, 0] [1, 1, 7, 4] [1, 1, 1, 1]  : memref<1x1x7x4xf32> to memref<1x1x7x4xf32>
  %2 = subview %output[0, 0, 0, 0] [1, 1, 4, 4] [1, 1, 1, 1]  : memref<1x1x4x4xf32> to memref<1x1x4x4xf32>
  // CHECK: linalg.conv
  linalg.conv(%0, %1, %2) {dilations = [2, 1], strides = [2, 2]} : memref<1x1x4x4xf32>, memref<1x1x7x4xf32>, memref<1x1x4x4xf32>
  return
}
