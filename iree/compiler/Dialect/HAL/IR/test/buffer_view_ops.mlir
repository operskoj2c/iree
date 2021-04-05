// RUN: iree-opt -allow-unregistered-dialect -split-input-file %s | iree-opt -allow-unregistered-dialect -split-input-file | IreeFileCheck %s

// CHECK-LABEL: @buffer_view_create
func @buffer_view_create(%arg0: !hal.buffer) -> !hal.buffer_view {
  %c32 = constant 32 : i32
  %0:2 = "test_hal.shape"() : () -> (index, index)
  // CHECK: %view = hal.buffer_view.create %arg0, element_type = %c32_i32, shape = [%0#0, %0#1] : !hal.buffer -> !hal.buffer_view
  %view = hal.buffer_view.create %arg0, element_type = %c32, shape = [%0#0, %0#1] : !hal.buffer -> !hal.buffer_view
  return %view : !hal.buffer_view
}

// -----

// CHECK-LABEL: @buffer_view_subview
func @buffer_view_subview(%arg0: !hal.buffer_view) -> !hal.buffer_view {
  %0:2 = "test_hal.indices"() : () -> (index, index)
  %1:2 = "test_hal.lengths"() : () -> (index, index)
  // CHECK: %view = hal.buffer_view.subview %arg0, indices = [%0#0, %0#1], lengths = [%1#0, %1#1] : !hal.buffer_view
  %view = hal.buffer_view.subview %arg0, indices = [%0#0, %0#1], lengths = [%1#0, %1#1] : !hal.buffer_view
  return %view : !hal.buffer_view
}

// -----

// CHECK-LABEL: @buffer_view_buffer
func @buffer_view_buffer(%arg0: !hal.buffer_view) -> !hal.buffer {
  // CHECK: %buffer = hal.buffer_view.buffer %arg0 : !hal.buffer
  %buffer = hal.buffer_view.buffer %arg0 : !hal.buffer
  return %buffer : !hal.buffer
}

// -----

// CHECK-LABEL: @buffer_view_byte_length
func @buffer_view_byte_length(%arg0: !hal.buffer_view) -> index {
  // CHECK: %len = hal.buffer_view.byte_length %arg0 : index
  %len = hal.buffer_view.byte_length %arg0 : index
  return %len : index
}

// -----

// CHECK-LABEL: @buffer_view_compute_offset
func @buffer_view_compute_offset(%arg0: !hal.buffer_view) -> index {
  %0:2 = "test_hal.indices"() : () -> (index, index)
  // CHECK: %off = hal.buffer_view.compute_offset %arg0, indices = [%0#0, %0#1]
  %off = hal.buffer_view.compute_offset %arg0, indices = [%0#0, %0#1] : index
  return %off : index
}

// -----

// CHECK-LABEL: @buffer_view_compute_range
func @buffer_view_compute_range(%arg0: !hal.buffer_view) -> (index, index) {
  %0:2 = "test_hal.indices"() : () -> (index, index)
  %1:2 = "test_hal.lengths"() : () -> (index, index)
  // CHECK: %off, %len = hal.buffer_view.compute_range %arg0, indices = [%0#0, %0#1], lengths = [%1#0, %1#1]
  %off, %len = hal.buffer_view.compute_range %arg0, indices = [%0#0, %0#1], lengths = [%1#0, %1#1] : index, index
  return %off, %len : index, index
}

// -----

// CHECK-LABEL: @buffer_view_shape_queries
func @buffer_view_shape_queries(%arg0: !hal.buffer_view) -> (index, index, index, index) {
  // CHECK: %{{.+}} = hal.buffer_view.rank %arg0 : index
  %0 = hal.buffer_view.rank %arg0 : index
  // CHECK: %{{.+}} = hal.buffer_view.dim %arg0, 0 : index
  %1 = hal.buffer_view.dim %arg0, 0 : index
  // CHECK: %{{.+}}:2 = hal.buffer_view.dims %arg0 : index, index
  %2, %3 = hal.buffer_view.dims %arg0 : index, index
  return %0, %1, %2, %3 : index, index, index, index
}
