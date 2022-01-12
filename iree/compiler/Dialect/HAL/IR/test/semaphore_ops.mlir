// RUN: iree-opt -split-input-file %s | iree-opt -split-input-file | FileCheck %s

// CHECK-LABEL: @semaphore_create
func @semaphore_create(%arg0 : !hal.device) -> !hal.semaphore {
  %c0 = arith.constant 0 : index
  // CHECK: %semaphore = hal.semaphore.create device(%arg0 : !hal.device) initial(%c0) : !hal.semaphore
  %semaphore = hal.semaphore.create device(%arg0 : !hal.device) initial(%c0) : !hal.semaphore
  return %semaphore : !hal.semaphore
}

// -----

// CHECK-LABEL: @semaphore_query
func @semaphore_query(%arg0 : !hal.semaphore) {
  // CHECK: = hal.semaphore.query<%arg0 : !hal.semaphore> : i32, index
  %status, %value = hal.semaphore.query<%arg0 : !hal.semaphore> : i32, index
  return
}

// -----

// CHECK-LABEL: @semaphore_signal
func @semaphore_signal(%arg0 : !hal.semaphore) {
  %c0 = arith.constant 0 : index
  // CHECK: hal.semaphore.signal<%arg0 : !hal.semaphore> value(%c0)
  hal.semaphore.signal<%arg0 : !hal.semaphore> value(%c0)
  return
}

// -----

// CHECK-LABEL: @semaphore_fail
func @semaphore_fail(%arg0 : !hal.semaphore) {
  // CHECK: %[[C0:.+]] = arith.constant 0
  %c0 = arith.constant 0 : i32
  // CHECK: hal.semaphore.fail<%arg0 : !hal.semaphore> status(%[[C0]])
  hal.semaphore.fail<%arg0 : !hal.semaphore> status(%c0)
  return
}

// -----

// CHECK-LABEL: @semaphore_await
func @semaphore_await(%arg0 : !hal.semaphore) {
  // CHECK: %[[C0:.+]] = arith.constant 0
  %c0 = arith.constant 0 : index
  // CHECK: = hal.semaphore.await<%arg0 : !hal.semaphore> until(%[[C0]]) : i32
  %0 = hal.semaphore.await<%arg0 : !hal.semaphore> until(%c0) : i32
  return
}
