// RUN: iree-opt -split-input-file -iree-mhlo-input-transformation-pipeline -iree-flow-transformation-pipeline %s | IreeFileCheck %s

// CHECK-LABEL: @empty
func @empty() {
  // CHECK-NEXT: return
  return
}

// -----

func @hloElementwiseOps(%arg0 : tensor<4xf32>) -> tensor<4xf32> {
  %0 = mhlo.add %arg0, %arg0 : tensor<4xf32>
  %1 = mhlo.subtract %0, %arg0 : tensor<4xf32>
  %2 = mhlo.multiply %1, %arg0 : tensor<4xf32>
  return %2 : tensor<4xf32>
}

// CHECK-LABEL: flow.executable private @hloElementwiseOps_dispatch_0 {
//  CHECK-NEXT:   flow.dispatch.entry public @hloElementwiseOps_dispatch_0
//  CHECK-NEXT:   module {
//  CHECK-NEXT:     func @hloElementwiseOps_dispatch_0(%arg0: !flow.dispatch.tensor<readonly:4xf32>, %arg1: !flow.dispatch.tensor<writeonly:4xf32>) {
//       CHECK:       %{{.+}} = linalg.generic
//       CHECK:         %{{.+}} = arith.addf %{{.+}}, %{{.+}} : f32
//  CHECK-NEXT:         %{{.+}} = arith.subf %{{.+}}, %{{.+}} : f32
//  CHECK-NEXT:         %{{.+}} = arith.mulf %{{.+}}, %{{.+}} : f32
//       CHECK: func @hloElementwiseOps(%arg0: tensor<4xf32>) -> tensor<4xf32> {
//  CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4xf32>) -> tensor<4xf32> =
//  CHECK-NEXT:       (%arg1: tensor<4xf32>) -> tensor<4xf32> {
//   CHECK-DAG:     %[[C1:.+]] = arith.constant 1 : index
//   CHECK-DAG:     %[[C4:.+]] = arith.constant 4 : index
//  CHECK-NEXT:     %1 = flow.dispatch @hloElementwiseOps_dispatch_0::@hloElementwiseOps_dispatch_0[%[[C4]], %[[C1]], %[[C1]]](%arg1) : (tensor<4xf32>) -> tensor<4xf32>
//  CHECK-NEXT:     flow.return %1 : tensor<4xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %0 : tensor<4xf32>
//  CHECK-NEXT: }

// -----

func @interleavedDot(%arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  %0 = mhlo.add %arg0, %arg0 : tensor<4x4xf32>
  %1 = "mhlo.dot"(%0, %arg0) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
  %2 = mhlo.multiply %1, %arg0 : tensor<4x4xf32>
  return %2 : tensor<4x4xf32>
}

// CHECK-LABEL: flow.executable private @interleavedDot_dispatch_0 {
//  CHECK-NEXT:   flow.dispatch.entry public @interleavedDot_dispatch_0
//  CHECK-NEXT:   module {
//  CHECK-NEXT:     func @interleavedDot_dispatch_0
//       CHECK:       %{{.+}} = linalg.generic
//       CHECK:         %{{.+}} = arith.addf %{{.+}}, %{{.+}} : f32
//       CHECK: flow.executable private @interleavedDot_dispatch_1 {
//  CHECK-NEXT:   flow.dispatch.entry public @interleavedDot_dispatch_1
//  CHECK-NEXT:   module {
//  CHECK-NEXT:     func @interleavedDot_dispatch_1
//       CHECK:       %{{.+}} = linalg.matmul
//       CHECK:       %{{.+}} = linalg.generic
//       CHECK:         %{{.+}} = arith.mulf %{{.+}}, %{{.+}} : f32
//       CHECK: func @interleavedDot(%arg0: tensor<4x4xf32>) -> tensor<4x4xf32> {
//  CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4x4xf32>) -> tensor<4x4xf32> =
//  CHECK-NEXT:        (%arg1: tensor<4x4xf32>) -> tensor<4x4xf32> {
//   CHECK-DAG:     %[[C1:.+]] = arith.constant 1 : index
//   CHECK-DAG:     %[[C4:.+]] = arith.constant 4 : index
//  CHECK-NEXT:     %1 = flow.dispatch @interleavedDot_dispatch_0::@interleavedDot_dispatch_0[%[[C4]], %[[C4]], %[[C1]]](%arg1) : (tensor<4x4xf32>) -> tensor<4x4xf32>
//  CHECK-NEXT:     %2 = flow.dispatch @interleavedDot_dispatch_1::@interleavedDot_dispatch_1[%[[C4]], %[[C4]], %[[C1]]](%arg1, %1) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
//  CHECK-NEXT:     flow.return %2 : tensor<4x4xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %0 : tensor<4x4xf32>
//  CHECK-NEXT: }

// -----

func @reduction(%arg0 : tensor<4x8xf32>) -> tensor<4xf32> {
  %0 = arith.constant dense<0.0> : tensor<f32>
  %1 = "mhlo.reduce"(%arg0, %0) ( {
  ^bb0(%arg1 : tensor<f32>, %arg2 : tensor<f32>):
    %2 = mhlo.add %arg1, %arg2 : tensor<f32>
    "mhlo.return"(%2) : (tensor<f32>) -> ()
  }) {dimensions = dense<[1]> : tensor<1xi64>} : (tensor<4x8xf32>, tensor<f32>) -> tensor<4xf32>
  return %1 : tensor<4xf32>
}

// CHECK-LABEL: flow.executable private @reduction_dispatch_0 {
//  CHECK-NEXT:   flow.dispatch.entry public @reduction_dispatch_0
//  CHECK-NEXT:   module {
//  CHECK-NEXT:     func @reduction_dispatch_0
//       CHECK:       %{{.+}} = linalg.generic
//       CHECK:         %{{.+}} = arith.addf %{{.+}}, %{{.+}} : f32
//       CHECK: func @reduction(%arg0: tensor<4x8xf32>) -> tensor<4xf32> {
//  CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4x8xf32>) -> tensor<4xf32> =
//  CHECK-NEXT:       (%arg1: tensor<4x8xf32>) -> tensor<4xf32> {
//   CHECK-DAG:     %[[C1:.+]] = arith.constant 1 : index
//   CHECK-DAG:     %[[C4:.+]] = arith.constant 4 : index
//  CHECK-NEXT:     %1 = flow.dispatch @reduction_dispatch_0::@reduction_dispatch_0[%[[C4]], %[[C1]], %[[C1]]](%arg1) : (tensor<4x8xf32>) -> tensor<4xf32>
//  CHECK-NEXT:     flow.return %1 : tensor<4xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %0 : tensor<4xf32>
//  CHECK-NEXT: }
