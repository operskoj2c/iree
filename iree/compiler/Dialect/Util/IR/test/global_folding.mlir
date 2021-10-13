// RUN: iree-opt -split-input-file -canonicalize %s | iree-opt -split-input-file | IreeFileCheck %s

// CHECK: util.global private @v_initialized = dense<4> : tensor<4xi32>
util.global private @v_initialized : tensor<4xi32>
// CHECK-NOT: util.initializer
util.initializer {
  %0 = arith.constant dense<4> : tensor<4xi32>
  util.global.store %0, @v_initialized : tensor<4xi32>
  util.initializer.return
}

// -----

util.global private @v_unused : tensor<4xi32>
// CHECK-LABEL: @unused_load
func @unused_load() {
  // CHECK-NEXT: return
  %0 = util.global.load @v_unused : tensor<4xi32>
  return
}

// -----

util.global private @v_const = dense<1.0> : tensor<8xf32>
// CHECK-LABEL: @fold_immutable_const
func @fold_immutable_const() -> tensor<8xf32> {
  // CHECK-NEXT: %[[CONST:.+]] = arith.constant dense<1.{{.+}}> : tensor<8xf32>
  %0 = util.global.load @v_const : tensor<8xf32>
  // CHECK-NEXT: return %[[CONST]] : tensor<8xf32>
  return %0 : tensor<8xf32>
}

// -----

util.global private @v_const {noinline} = dense<1.0> : tensor<8xf32>
// CHECK-LABEL: @no_fold_noinline_immutable_const
func @no_fold_noinline_immutable_const() -> tensor<8xf32> {
  // CHECK-NEXT: = util.global.load @v_const : tensor<8xf32>
  %0 = util.global.load @v_const : tensor<8xf32>
  return %0 : tensor<8xf32>
}

// -----

util.global private mutable @v_nop : tensor<4xi32>
// CHECK-LABEL: @nop_load_store
func @nop_load_store() {
  // CHECK-NEXT: return
  %0 = util.global.load @v_nop : tensor<4xi32>
  util.global.store %0, @v_nop : tensor<4xi32>
  return
}

// -----

util.global private @v : tensor<4xf32>
// CHECK-LABEL: @fold_load_indirect
func @fold_load_indirect() -> tensor<4xf32> {
  %0 = util.global.address @v : !util.ptr<tensor<4xf32>>
  // CHECK-NEXT: = util.global.load @v
  %1 = util.global.load.indirect %0 : !util.ptr<tensor<4xf32>> -> tensor<4xf32>
  return %1 : tensor<4xf32>
}

// -----

util.global private mutable @v : tensor<4xf32>
// CHECK-LABEL: @fold_store_indirect
func @fold_store_indirect(%arg0 : tensor<4xf32>) {
  %0 = util.global.address @v : !util.ptr<tensor<4xf32>>
  // CHECK-NEXT: util.global.store %arg0, @v
  util.global.store.indirect %arg0, %0 : tensor<4xf32> -> !util.ptr<tensor<4xf32>>
  return
}
