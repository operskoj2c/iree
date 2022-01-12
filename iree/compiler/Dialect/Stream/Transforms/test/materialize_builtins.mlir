// RUN: iree-opt -split-input-file -iree-stream-materialize-builtins %s | FileCheck %s

// Tests expansion of the stream.builtin.splat.i64 op.

// CHECK: stream.executable private @__builtin_splat_i64

// CHECK-LABEL: @builtinSplatI64
func @builtinSplatI64(%arg0: index, %arg1: i64) -> !stream.resource<*> {
  // CHECK: %[[COUNT:.+]] = arith.divui %arg0, %c8
  // CHECK: %[[RET:.+]] = stream.async.dispatch @__builtin_splat_i64::@__builtin_splat_i64[%[[COUNT]], %c1, %c1](%arg1, %[[COUNT]]) : (i64, index) -> !stream.resource<*>{%arg0}
  %0 = stream.builtin.splat.i64 %arg1 : i64 -> !stream.resource<*>{%arg0}
  // CHECK: return %[[RET]]
  return %0 : !stream.resource<*>
}

// -----

// Tests expansion of the stream.builtin.fill.i64 op.

// CHECK: stream.executable private @__builtin_fill_i64

// CHECK-LABEL: @builtinFillI64
// CHECK-SAME: (%[[RES:.+]]: !stream.resource<*>, %[[SIZE:.+]]: index, %[[VALUE:.+]]: i64, %[[BYTE_OFFSET:.+]]: index, %[[BYTE_LENGTH:.+]]: index)
func @builtinFillI64(%res: !stream.resource<*>, %size: index, %value: i64, %byte_offset: index, %byte_length: index) -> !stream.resource<*> {
  // CHECK: %[[COUNT:.+]] = arith.divui %[[BYTE_LENGTH]], %c8
  // CHECK: %[[RET:.+]] = stream.async.dispatch @__builtin_fill_i64::@__builtin_fill_i64[%[[COUNT]], %c1, %c1](%[[RES]], %[[VALUE]], %[[BYTE_OFFSET]], %[[COUNT]]) : (!stream.resource<*>{%[[SIZE]]}, i64, index, index) -> %[[RES]]{%[[SIZE]]}
  %0 = stream.builtin.fill.i64 %value, %res[%byte_offset to %byte_length for %byte_length] : i64 -> %arg0 as !stream.resource<*>{%size}
  // CHECK: return %[[RET]]
  return %0 : !stream.resource<*>
}

// -----

// Tests that builtins used in multiple functions share the same executable.

// CHECK: stream.executable private @__builtin_splat_i64
// CHECK-NOT: stream.executable private @__builtin_splat_i64

// CHECK: util.initializer
util.initializer {
  %c128 = arith.constant 128 : index
  %c0_i64 = arith.constant 0 : i64
  // CHECK: = stream.async.dispatch @__builtin_splat_i64::@__builtin_splat_i64
  %0 = stream.builtin.splat.i64 %c0_i64 : i64 -> !stream.resource<*>{%c128}
  util.initializer.return
}

// CHECK: func @otherUser
func @otherUser() -> !stream.resource<*> {
  %c128 = arith.constant 128 : index
  %c1_i64 = arith.constant 1 : i64
  // CHECK: %[[RET:.+]] = stream.async.dispatch @__builtin_splat_i64::@__builtin_splat_i64
  %0 = stream.builtin.splat.i64 %c1_i64 : i64 -> !stream.resource<*>{%c128}
  // CHECK: return %[[RET]]
  return %0 : !stream.resource<*>
}
