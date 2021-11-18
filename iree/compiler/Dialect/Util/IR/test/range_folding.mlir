// RUN: iree-opt -split-input-file -canonicalize %s | iree-opt -split-input-file | IreeFileCheck %s

// NOTE: util.range.min and util.range.max share their code so we just test min.

// CHECK-LABEL: @rangeMinConstant
func @rangeMinConstant() -> (index, index) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  // CHECK-DAG: %[[C0:.+]] = arith.constant 0
  %0 = util.range.min %c0 : index
  // CHECK-DAG: %[[C1:.+]] = arith.constant 1
  %1 = util.range.min %c3, %c1, %c2 : index
  // CHECK: return %[[C0]], %[[C1]]
  return %0, %1 : index, index
}

// -----

// CHECK-LABEL: @rangeMinExpand
func @rangeMinExpand(%arg0: index, %arg1: index) -> index {
  // CHECK: %[[MIN:.+]] = arith.minui %arg0, %arg1 : index
  %0 = util.range.min %arg0, %arg1 : index
  // CHECK: return %[[MIN]]
  return %0 : index
}

// -----

// CHECK-LABEL: @rangeMinSimplify
func @rangeMinSimplify(%arg0: index, %arg1: index) -> (index, index) {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  // CHECK: %[[MIN0:.+]] = util.range.min %arg0, %arg1, %c0 : index
  %0 = util.range.min %arg0, %c0, %arg0, %arg1 : index
  // CHECK: %[[MIN1:.+]] = util.range.min %arg0, %arg1, %c1 : index
  %1 = util.range.min %c3, %arg0, %c1, %arg1, %c2, %arg1 : index
  // CHECK: return %[[MIN0]], %[[MIN1]]
  return %0, %1 : index, index
}

// -----

// CHECK-LABEL: @rangeExtentsFoldConstants
func @rangeExtentsFoldConstants() -> (index, index) {
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  %0:2 = util.range.extents [%c1 for %c2], [%c2 for %c3] : index
  // CHECK: return %c1, %c4
  return %0#0, %0#1 : index, index
}

// -----

// CHECK-LABEL: @rangeExtentsFoldConstantsDynamic
func @rangeExtentsFoldConstantsDynamic(%arg0: index, %arg1: index) -> (index, index) {
  %c1 = arith.constant 1 : index
  %c2 = arith.constant 2 : index
  %c3 = arith.constant 3 : index
  // CHECK: %[[RANGE_MAX_EXC:.+]] = arith.addi %arg0, %arg1
  // CHECK: %[[RANGE_MAX_INC:.+]] = arith.subi %[[RANGE_MAX_EXC]], %c1
  // CHECK: %[[RANGE_MIN:.+]] = arith.minui %arg0, %c1
  // CHECK: %[[RANGE_MAX:.+]] = arith.maxui %[[RANGE_MAX_INC]], %c4
  %0:2 = util.range.extents [%c1 for %c2], [%arg0 for %arg1], [%c2 for %c3] : index
  // CHECK: return %[[RANGE_MIN]], %[[RANGE_MAX]]
  return %0#0, %0#1 : index, index
}

// -----

// CHECK-LABEL: @rangeExtentsExpand1
func @rangeExtentsExpand1(%arg0: index, %arg1: index) -> (index, index) {
  // CHECK: %[[RANGE_MAX_EXC:.+]] = arith.addi %arg0, %arg1
  // CHECK: %[[RANGE_MAX_INC:.+]] = arith.subi %[[RANGE_MAX_EXC]], %c1
  %0:2 = util.range.extents [%arg0 for %arg1] : index
  // CHECK: return %arg0, %[[RANGE_MAX_INC]]
  return %0#0, %0#1 : index, index
}

// -----

// CHECK-LABEL: @rangeExtentsExpand2
func @rangeExtentsExpand2(%arg0: index, %arg1: index, %arg2: index, %arg3: index) -> (index, index) {
  // CHECK: %[[RANGE_MIN:.+]] = arith.minui %arg0, %arg2
  // CHECK: %[[RANGE0_MAX_EXC:.+]] = arith.addi %arg0, %arg1
  // CHECK: %[[RANGE0_MAX_INC:.+]] = arith.subi %[[RANGE0_MAX_EXC]], %c1
  // CHECK: %[[RANGE1_MAX_EXC:.+]] = arith.addi %arg2, %arg3
  // CHECK: %[[RANGE1_MAX_INC:.+]] = arith.subi %[[RANGE1_MAX_EXC]], %c1
  // CHECK: %[[RANGE_MAX:.+]] = arith.maxui %[[RANGE0_MAX_INC]], %[[RANGE1_MAX_INC]]
  %0:2 = util.range.extents [%arg0 for %arg1], [%arg2 for %arg3] : index
  // CHECK: return %[[RANGE_MIN]], %[[RANGE_MAX]]
  return %0#0, %0#1 : index, index
}

// -----

// CHECK-LABEL: @rangeExtentsDeduplicate
func @rangeExtentsDeduplicate(%arg0: index, %arg1: index, %arg2: index, %arg3: index, %arg4: index, %arg5: index) -> (index, index) {
  // CHECK: = util.range.extents [%arg0 for %arg1], [%arg2 for %arg3], [%arg4 for %arg5] : index
  %0:2 = util.range.extents [%arg0 for %arg1], [%arg2 for %arg3], [%arg0 for %arg1], [%arg4 for %arg5] : index
  return %0#0, %0#1 : index, index
}
