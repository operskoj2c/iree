// RUN: iree-run-mlir --iree-input-type=mhlo -iree-hal-target-backends=vmvx %s | IreeFileCheck %s
// RUN: [[ $IREE_VULKAN_DISABLE == 1 ]] || (iree-run-mlir --iree-input-type=mhlo -iree-hal-target-backends=vulkan-spirv %s | IreeFileCheck %s)

module {
  util.global private mutable @counter = dense<2.0> : tensor<f32>

  // CHECK: EXEC @get_state
  func @get_state() -> tensor<f32> {
    %0 = util.global.load @counter : tensor<f32>
    return %0 : tensor<f32>
  }
  // CHECK: f32=2

  // CHECK: EXEC @inc
  func @inc() -> tensor<f32> {
    %0 = util.global.load @counter : tensor<f32>
    %c1 = arith.constant dense<1.0> : tensor<f32>
    %1 = mhlo.add %0, %c1 : tensor<f32>
    util.global.store %1, @counter : tensor<f32>
    %2 = util.global.load @counter : tensor<f32>
    return %2 : tensor<f32>
  }
  // CHECK: f32=3
}
