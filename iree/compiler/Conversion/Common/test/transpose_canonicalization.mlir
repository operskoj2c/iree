// RUN: iree-opt %s -iree-codegen-optimize-vector-transfer | IreeFileCheck %s

// CHECK-LABEL: func @transpose
//  CHECK-NEXT:   vector.shape_cast %{{.*}} : vector<1x1x4xf32> to vector<1x4x1xf32>
func @transpose(%arg0: vector<1x1x4xf32>) -> vector<1x4x1xf32> {
  %0 = vector.transpose %arg0, [0, 2, 1] : vector<1x1x4xf32> to vector<1x4x1xf32>
  return %0: vector<1x4x1xf32>
}
