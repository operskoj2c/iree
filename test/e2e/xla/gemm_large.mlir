// RUN: iree-run-mlir %s -iree-hal-target-backends=vmla -input-value="32x1024xf32=1" -input-value="1024x64xf32=0.4" | IreeFileCheck %s
// RUN: iree-run-mlir %s -iree-hal-target-backends=llvm-ir -input-value="32x1024xf32=1" -input-value="1024x64xf32=0.4" | IreeFileCheck %s
// RUN: [[ $IREE_VULKAN_DISABLE == 1 ]] || (iree-run-mlir -iree-hal-target-backends=vulkan-spirv %s -input-value="32x1024xf32=1" -input-value="1024x64xf32=0.4" | IreeFileCheck %s)
// RUN: [[ $IREE_VULKAN_DISABLE == 1 ]] || (iree-run-mlir -iree-hal-target-backends=vulkan-spirv -iree-use-linalg-to-spirv-path %s -input-value="32x1024xf32=1" -input-value="1024x64xf32=0.4" | IreeFileCheck %s)

// CHECK-LABEL: @large_matmul
func @large_matmul(%arg0: tensor<32x1024xf32>, %arg1: tensor<1024x64xf32>) -> tensor<32x64xf32> {
  %0 = "xla_hlo.dot"(%arg0, %arg1) : (tensor<32x1024xf32>, tensor<1024x64xf32>) -> tensor<32x64xf32>
  return %0 : tensor<32x64xf32>
}

// CHECK: 32x64xf32=[409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596 409.596][...][...][...][...][...][...][...][...][...][...][...][...][...][...][...][...]
