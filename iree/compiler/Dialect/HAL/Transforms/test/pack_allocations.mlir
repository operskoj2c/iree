// RUN: iree-opt -split-input-file -iree-hal-pack-allocations -cse -canonicalize %s | IreeFileCheck %s

module attributes {
  hal.device.targets = [
    #hal.device.target<"cpu", {
      buffer_constraints = #hal.buffer_constraints<max_allocation_size = 1073741824, min_buffer_offset_alignment = 16, max_buffer_range = 1073741824, min_buffer_range_alignment = 16>
    }>
  ]
} {

// CHECK-LABEL: @packStatic
// CHECK-SAME: %[[ALLOCATOR:.+]]: !hal.allocator
func @packStatic(%allocator: !hal.allocator) ->
    (index, index, index, index, index, index, index) {
  %c100 = arith.constant 100 : index
  %c200 = arith.constant 200 : index
  %t:7 = hal.allocator.pack<%allocator : !hal.allocator> slices({
    [0, 1] = %c100,  // +0
    [1, 2] = %c100,  // +112 (100 align 16)
    [2, 3] = %c100,  // +0 (reuse [0, 1])
    [0, 4] = %c200,  // +224 (after 112 + 112; end align 16)
    [5, 6] = %c200,  // +0 (reuse [0, 1]/[2, 3])
    [5, 8] = %c100,  // +208 (after 200 align 16)
  }) : index
  // 224 + 200 align 16 = 432 total bytes required
  // CHECK: return %c432
  // CHECK-SAME: %c0, %c112, %c0, %c224, %c0, %c208
  return %t#0, %t#1, %t#2, %t#3, %t#4, %t#5, %t#6 : index, index, index, index, index, index, index
}

}

// -----

module attributes {
  hal.device.targets = [
    #hal.device.target<"cpu", {
      buffer_constraints = #hal.buffer_constraints<max_allocation_size = 1073741824, min_buffer_offset_alignment = 16, max_buffer_range = 1073741824, min_buffer_range_alignment = 16>
    }>
  ]
} {

// CHECK-LABEL: @packDynamic
// CHECK-SAME: %[[ALLOCATOR:.+]]: !hal.allocator,
// CHECK-SAME: %[[SIZE_A:.+]]: index, %[[SIZE_B:.+]]: index
func @packDynamic(%allocator: !hal.allocator, %size_a: index, %size_b: index) ->
    (index, index, index, index) {
  %t:4 = hal.allocator.pack<%allocator : !hal.allocator> slices({
    [0, 1] = %size_a,
    [1, 2] = %size_b,
    [2, 3] = %size_a,
  }) : index

  // CHECK-DAG: %c16 = arith.constant 16 : index
  // CHECK-DAG: %c0 = arith.constant 0 : index
  // CHECK-DAG: %0 = util.align %arg1, %c16 : index
  // CHECK-DAG: %1 = util.align %arg2, %c16 : index
  // CHECK-DAG: %2 = arith.addi %0, %1 : index

  // CHECK-DAG: return %2, %c0, %0, %c0
  return %t#0, %t#1, %t#2, %t#3 : index, index, index, index
}

}

// -----

module attributes {
  hal.device.targets = [
    #hal.device.target<"cpu", {
      buffer_constraints = #hal.buffer_constraints<max_allocation_size = 1073741824, min_buffer_offset_alignment = 16, max_buffer_range = 1073741824, min_buffer_range_alignment = 16>
    }>
  ]
} {

// CHECK-LABEL: @packMixedStaticDynamic
// CHECK-SAME: %[[ALLOCATOR:.+]]: !hal.allocator,
// CHECK-SAME: %[[SIZE_A:.+]]: index, %[[SIZE_B:.+]]: index
func @packMixedStaticDynamic(%allocator: !hal.allocator, %size_a: index, %size_b: index) ->
    (index, index, index, index, index) {
  %c100 = arith.constant 100 : index
  %c200 = arith.constant 200 : index
  %t:5 = hal.allocator.pack<%allocator : !hal.allocator> slices({
    [0, 1] = %c100,
    [1, 2] = %size_a,
    [2, 3] = %size_b,
    [5, 6] = %c200,
  }) : index

  // CHECK-DAG: %c16 = arith.constant 16 : index
  // CHECK-DAG: %c208 = arith.constant 208 : index
  // CHECK-DAG: %c0 = arith.constant 0 : index
  // CHECK-DAG: %0 = util.align %arg1, %c16 : index
  // CHECK-DAG: %1 = arith.addi %0, %c208 : index
  // CHECK-DAG: %2 = util.align %arg2, %c16 : index
  // CHECK-DAG: %3 = arith.addi %1, %2 : index

  // CHECK-DAG: return %3, %c0, %c208, %1, %c0
  return %t#0, %t#1, %t#2, %t#3, %t#4 : index, index, index, index, index
}

}
