// RUN: iree-opt -split-input-file -canonicalize %s | iree-opt -split-input-file | FileCheck %s

// CHECK-LABEL: @foldSameAlignment
// CHECK-SAME: (%[[VALUE:.+]]: index, %[[ALIGNMENT:.+]]: index)
func @foldSameAlignment(%value: index, %alignment: index) -> index {
  // CHECK: %[[RET:.+]] = util.align %[[VALUE]], %[[ALIGNMENT]]
  %0 = util.align %value, %alignment : index
  // CHECK-NOT: util.align
  %1 = util.align %0, %alignment : index
  // CHECK: return %[[RET]]
  return %1 : index
}

// -----

// CHECK-LABEL: @foldGreaterAlignment
// CHECK-SAME: (%[[VALUE:.+]]: index)
func @foldGreaterAlignment(%value: index) -> index {
  %c8 = arith.constant 8 : index
  %c16 = arith.constant 16 : index
  // CHECK: %[[RET:.+]] = util.align %[[VALUE]], %c16
  %0 = util.align %value, %c16 : index
  // CHECK-NOT: util.align
  %1 = util.align %0, %c8 : index
  // CHECK: return %[[RET]]
  return %1 : index
}

// -----

// CHECK-LABEL: @dontFoldLesserAlignment
// CHECK-SAME: (%[[VALUE:.+]]: index)
func @dontFoldLesserAlignment(%value: index) -> index {
  %c8 = arith.constant 8 : index
  %c16 = arith.constant 16 : index
  // CHECK: %[[ALIGN16:.+]] = util.align %[[VALUE]], %c8
  %0 = util.align %value, %c8 : index
  // CHECK: %[[ALIGN8:.+]] = util.align %[[ALIGN16]], %c16
  %1 = util.align %0, %c16 : index
  // CHECK: return %[[ALIGN8]]
  return %1 : index
}

// -----

// CHECK-LABEL: @foldAlignmentRecursively
// CHECK-SAME: (%[[VALUE:.+]]: index, %[[ALIGNMENT:.+]]: index)
func @foldAlignmentRecursively(%value: index, %alignment: index) -> index {
  %c16 = arith.constant 16 : index
  // CHECK: %[[ALIGN16:.+]] = util.align %[[VALUE]], %c16
  %0 = util.align %value, %c16 : index
  // CHECK: %[[ALIGN_DYNAMIC:.+]] = util.align %[[ALIGN16]], %[[ALIGNMENT]]
  %1 = util.align %0, %alignment : index
  // CHECK-NOT: util.align
  %2 = util.align %1, %c16 : index
  // CHECK: return %[[ALIGN_DYNAMIC]]
  return %2 : index
}

// -----

// CHECK-LABEL: @foldAddAlignment
// CHECK-SAME: (%[[LHS:.+]]: index, %[[RHS:.+]]: index, %[[ALIGNMENT:.+]]: index)
func @foldAddAlignment(%lhs: index, %rhs: index, %alignment: index) -> index {
  // CHECK: %[[LHS_ALIGNED:.+]] = util.align %[[LHS]], %[[ALIGNMENT]]
  %lhs_aligned = util.align %lhs, %alignment : index
  // CHECK: %[[RHS_ALIGNED:.+]] = util.align %[[RHS]], %[[ALIGNMENT]]
  %rhs_aligned = util.align %rhs, %alignment : index
  // CHECK: %[[SUM_ALIGNED:.+]] = arith.addi %[[LHS_ALIGNED]], %[[RHS_ALIGNED]]
  %sum_aligned = arith.addi %lhs_aligned, %rhs_aligned : index
  // CHECK-NOT: util.align
  %result = util.align %sum_aligned, %alignment : index
  // CHECK: return %[[SUM_ALIGNED]]
  return %result : index
}

// -----

// CHECK-LABEL: @foldAddAlignmentConstant
// CHECK-SAME: (%[[LHS:.+]]: index)
func @foldAddAlignmentConstant(%lhs: index) -> index {
  %c16 = arith.constant 16 : index
  %c32 = arith.constant 32 : index
  %c64 = arith.constant 64 : index
  // CHECK: %[[LHS_ALIGNED:.+]] = util.align %[[LHS]], %c64
  %lhs_aligned = util.align %lhs, %c64 : index
  // CHECK: %[[SUM_ALIGNED:.+]] = arith.addi %[[LHS_ALIGNED]], %c32
  %sum_aligned = arith.addi %lhs_aligned, %c32 : index
  // CHECK-NOT: util.align
  %result = util.align %sum_aligned, %c16 : index
  // CHECK: return %[[SUM_ALIGNED]]
  return %result : index
}
