// RUN: iree-opt -split-input-file -iree-vm-conversion -iree-vm-target-extension=f32 %s | IreeFileCheck %s

module {
  // CHECK-LABEL: vm.func private @arithmetic
  func @arithmetic(%arg0: f32) -> f32 {

    // CHECK: vm.atan.f32
    %0 = math.atan %arg0 : f32

    // CHECK: vm.atan2.f32
    %1 = math.atan2 %arg0, %0 : f32

    // CHECK: vm.cos.f32
    %2 = math.cos %1 : f32

    // CHECK: vm.sin.f32
    %3 = math.sin %2 : f32

    // CHECK: vm.exp.f32
    %4 = math.exp %3 : f32

    // CHECK: vm.exp2.f32
    %5 = math.exp2 %4 : f32

    // CHECK: vm.expm1.f32
    %6 = math.expm1 %5 : f32

    // CHECK: vm.log.f32
    %7 = math.log %6 : f32

    // CHECK: vm.log10.f32
    %8 = math.log10 %7 : f32

    // CHECK: vm.log1p.f32
    %9 = math.log1p %8 : f32

    // CHECK: vm.log2.f32
    %10 = math.log2 %9 : f32

    // CHECK: vm.pow.f32
    %11 = math.powf %arg0, %10 : f32

    // CHECK: vm.rsqrt.f32
    %12 = math.rsqrt %11 : f32

    // CHECK: vm.sqrt.f32
    %13 = math.sqrt %12 : f32

    // CHECK: vm.tanh.f32
    %14 = math.tanh %13 : f32

    return %14 : f32
  }
}
