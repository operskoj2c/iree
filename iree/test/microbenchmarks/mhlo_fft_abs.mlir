//===----------------------------------------------------------------------===//
// rfft + abs ops
//===----------------------------------------------------------------------===//

func @rfft_abs_6x1024() -> tensor<6x513xf32> {
  %input = iree.unfoldable_constant dense<1.0> : tensor<6x1024xf32>
  %0 = "mhlo.fft"(%input) {
    fft_length = dense<1024> : tensor<1xi64>,
    fft_type = "RFFT"
  } : (tensor<6x1024xf32>) -> tensor<6x513xcomplex<f32>>
  %1 = "mhlo.abs"(%0) : (tensor<6x513xcomplex<f32>>) -> tensor<6x513xf32>
  return %1: tensor<6x513xf32>
}
