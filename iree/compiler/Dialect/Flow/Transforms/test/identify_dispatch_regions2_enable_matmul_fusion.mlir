// RUN: iree-opt -split-input-file -iree-flow-dispatchability-analysis -iree-flow-identify-dispatch-regions2 -iree-enable-matmul-fusion %s | IreeFileCheck %s

func @simpleDotAddMul
  (%arg0 : tensor<16x32xf32>, %arg1 : tensor<32x48xf32>,
   %arg2 : tensor<16x48xf32>, %arg3 : tensor<16x48xf32>) -> tensor<16x48xf32> {
  %0 = "mhlo.dot"(%arg0, %arg1) :
    (tensor<16x32xf32>, tensor<32x48xf32>) -> tensor<16x48xf32>
  %1 = mhlo.add %0, %arg2 : tensor<16x48xf32>
  %2 = mhlo.multiply %1, %arg3 : tensor<16x48xf32>
  return %2 : tensor<16x48xf32>
}
// CHECK-LABEL: func @simpleDotAddMul
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9_]+]]: tensor<16x32xf32>
//  CHECK-SAME:   %[[ARG1:[a-zA-Z0-9_]+]]: tensor<32x48xf32>
//  CHECK-SAME:   %[[ARG2:[a-zA-Z0-9_]+]]: tensor<16x48xf32>
//  CHECK-SAME:   %[[ARG3:[a-zA-Z0-9_]+]]: tensor<16x48xf32>
//  CHECK-NEXT:   %[[WORKLOAD:.+]] = constant 768
//  CHECK-NEXT:   %[[RESULT:.+]] = flow.dispatch.region[%[[WORKLOAD]] : index]
//  CHECK-SAME:     %[[ARG4:[a-zA-Z0-9_]+]] = %[[ARG0]]
//  CHECK-SAME:     %[[ARG5:[a-zA-Z0-9_]+]] = %[[ARG1]]
//  CHECK-SAME:     %[[ARG6:[a-zA-Z0-9_]+]] = %[[ARG2]]
//  CHECK-SAME:     %[[ARG7:[a-zA-Z0-9_]+]] = %[[ARG3]]
//  CHECK-SAME:     {
//  CHECK-NEXT:       %[[T1:.+]] = "mhlo.dot"(%[[ARG4]], %[[ARG5]])
//  CHECK-NEXT:       %[[T2:.+]] = mhlo.add %[[T1]], %[[ARG6]]
//  CHECK-NEXT:       %[[T3:.+]] = mhlo.multiply %[[T2]], %[[ARG7]]
//  CHECK-NEXT:       flow.return %[[T3]]
//  CHECK-NEXT:     }
//  CHECK-NEXT:   return %[[RESULT]]

// -----

func @twoDots
  (%arg0 : tensor<16x32xf32>, %arg1 : tensor<32x48xf32>,
   %arg2 : tensor<16x48xf32>, %arg3 : tensor<16x64xf32>,
   %arg4 : tensor<16x64xf32>) -> tensor<16x64xf32> {
  %0 = "mhlo.dot"(%arg0, %arg1) :
    (tensor<16x32xf32>, tensor<32x48xf32>) -> tensor<16x48xf32>
  %1 = mhlo.add %0, %arg2 : tensor<16x48xf32>
  %2 = "mhlo.dot"(%1, %arg3) :
    (tensor<16x48xf32>, tensor<16x64xf32>) -> tensor<16x64xf32>
  %3 = mhlo.multiply %2, %arg4 : tensor<16x64xf32>
  return %3 : tensor<16x64xf32>
}
// CHECK-LABEL: func @twoDots
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9_]+]]: tensor<16x32xf32>
//  CHECK-SAME:   %[[ARG1:[a-zA-Z0-9_]+]]: tensor<32x48xf32>
//  CHECK-SAME:   %[[ARG2:[a-zA-Z0-9_]+]]: tensor<16x48xf32>
//  CHECK-SAME:   %[[ARG3:[a-zA-Z0-9_]+]]: tensor<16x64xf32>
//  CHECK-SAME:   %[[ARG4:[a-zA-Z0-9_]+]]: tensor<16x64xf32>
//  CHECK-NEXT:   %[[WORKLOAD1:.+]] = constant 1024
//  CHECK-NEXT:   %[[WORKLOAD2:.+]] = constant 768
//  CHECK-NEXT:   %[[RESULT1:.+]] = flow.dispatch.region[%[[WORKLOAD2]] : index]
//  CHECK-SAME:     %[[ARG5:[a-zA-Z0-9_]+]] = %[[ARG0]]
//  CHECK-SAME:     %[[ARG6:[a-zA-Z0-9_]+]] = %[[ARG1]]
//  CHECK-SAME:     %[[ARG7:[a-zA-Z0-9_]+]] = %[[ARG2]]
//  CHECK-SAME:     {
//  CHECK-NEXT:       %[[T1:.+]] = "mhlo.dot"(%[[ARG5]], %[[ARG6]])
//  CHECK-NEXT:       %[[T2:.+]] = mhlo.add %[[T1]], %[[ARG7]]
//  CHECK-NEXT:       flow.return %[[T2]]
//  CHECK-NEXT:     }
//  CHECK-NEXT:   %[[RESULT2:.+]] = flow.dispatch.region[%[[WORKLOAD1]] : index]
//  CHECK-SAME:     %[[ARG5:[a-zA-Z0-9_]+]] = %[[RESULT1]]
//  CHECK-SAME:     %[[ARG6:[a-zA-Z0-9_]+]] = %[[ARG3]]
//  CHECK-SAME:     %[[ARG7:[a-zA-Z0-9_]+]] = %[[ARG4]]
//  CHECK-SAME:     {
//  CHECK-NEXT:       %[[T3:.+]] = "mhlo.dot"(%[[ARG5]], %[[ARG6]])
//  CHECK-NEXT:       %[[T4:.+]] = mhlo.multiply %[[T3]], %[[ARG7]]
//  CHECK-NEXT:       flow.return %[[T4]]
//  CHECK-NEXT:     }
//  CHECK-NEXT:   return %[[RESULT2]]

// -----

func @moveDispatchOp
  (%arg0 : tensor<1x384x384xf32>, %arg1 : tensor<384x512xf32>,
   %arg2 : tensor<512xf32>) -> tensor<1x384x512xf32> {
  %0 = "mhlo.reshape"(%arg0) : (tensor<1x384x384xf32>) -> tensor<384x384xf32>
  %1 = "mhlo.dot"(%0, %arg1) :
    (tensor<384x384xf32>, tensor<384x512xf32>) -> tensor<384x512xf32>
  %2 = "mhlo.broadcast_in_dim"(%arg2)
    {broadcast_dimensions = dense<1> : tensor<1xi64>} :
    (tensor<512xf32>) -> tensor<384x512xf32>
  %3 = mhlo.add %1, %2 : tensor<384x512xf32>
  %4 = "mhlo.reshape"(%3) : (tensor<384x512xf32>) -> tensor<1x384x512xf32>
  return %4 : tensor<1x384x512xf32>
}
// CHECK-LABEL: func @moveDispatchOp
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9_]+]]: tensor<1x384x384xf32>
//  CHECK-SAME:   %[[ARG1:[a-zA-Z0-9_]+]]: tensor<384x512xf32>
//  CHECK-SAME:   %[[ARG2:[a-zA-Z0-9_]+]]: tensor<512xf32>
//       CHECK:   %[[RESULT1:.+]] = flow.dispatch.region
//  CHECK-SAME:     %[[ARG3:[a-zA-Z0-9_]+]] = %[[ARG2]]
//  CHECK-SAME:     {
//  CHECK-NEXT:       %[[T1:.+]] = "mhlo.broadcast_in_dim"(%[[ARG3]])
//  CHECK-NEXT:       flow.return %[[T1]]
//  CHECK-NEXT:     }
//  CHECK-NEXT:   %[[RESULT2:.+]] = flow.dispatch.region
//  CHECK-SAME:     %[[ARG3:[a-zA-Z0-9_]+]] = %[[ARG1]]
//  CHECK-SAME:     %[[ARG4:[a-zA-Z0-9_]+]] = %[[RESULT1]]
//  CHECK-SAME:     %[[ARG5:[a-zA-Z0-9_]+]] = %[[ARG0]]
//  CHECK-SAME:     {
//  CHECK-NEXT:       %[[T2:.+]] = "mhlo.reshape"(%[[ARG5]])
//  CHECK-NEXT:       %[[T3:.+]] = "mhlo.dot"(%[[T2]], %[[ARG3]])
//  CHECK-NEXT:       %[[T4:.+]] = mhlo.add %[[T3]], %[[ARG4]]
//  CHECK-NEXT:       %[[T5:.+]] = "mhlo.reshape"(%[[T4]])
//  CHECK-NEXT:       flow.return %[[T5]]
//  CHECK-NEXT:     }
//  CHECK-NEXT:   return %[[RESULT2]]
