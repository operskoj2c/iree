// RUN: iree-opt -split-input-file %s | IreeFileCheck %s

// CHECK-LABEL: func @sort_tensor
// CHECK:         linalg_ext.sort
// CHECK-SAME:      outs({{.*}})
// CHECK:           linalg_ext.yield
func @sort_tensor(%arg0: tensor<128xi32>) -> tensor<128xi32> {
  %0 = linalg_ext.sort
    outs(%arg0 : tensor<128xi32>) {
  ^bb0(%arg1: i32, %arg2: i32):  // no predecessors
    %1 = arith.cmpi sgt, %arg1, %arg2 : i32
    linalg_ext.yield %1 : i1
  } -> tensor<128xi32>
  return %0 : tensor<128xi32>
}

// -----

// CHECK-LABEL: func @sort_memref
// CHECK:         linalg_ext.sort
// CHECK-SAME:      outs({{.*}})
// CHECK:           linalg_ext.yield
func @sort_memref(%arg0: memref<128xi32>) {
  linalg_ext.sort dimension(0)
    outs(%arg0 : memref<128xi32>) {
  ^bb0(%arg1: i32, %arg2: i32):  // no predecessors
    %0 = arith.cmpi sgt, %arg1, %arg2 : i32
    linalg_ext.yield %0 : i1
  }
  return
}

// -----

func @sort_multi_result_tensor(
    %arg0: tensor<?x?xi32>, %arg1: tensor<?x?xf32>)
    -> (tensor<?x?xi32>, tensor<?x?xf32>) {
  %0:2 = linalg_ext.sort dimension(0)
      outs(%arg0, %arg1 : tensor<?x?xi32>, tensor<?x?xf32>) {
      ^bb0(%arg2: i32, %arg3: i32, %arg4 : f32, %arg5 : f32):  // no predecessors
        %1 = arith.cmpf ogt, %arg4, %arg5 : f32
        linalg_ext.yield %1 : i1
      } -> tensor<?x?xi32>, tensor<?x?xf32>
  return %0#0, %0#1 : tensor<?x?xi32>, tensor<?x?xf32>
}
// CHECK-LABEL: func @sort_multi_result_tensor
//  CHECK-SAME:   %[[ARG0:.+]]: tensor<?x?xi32>
//  CHECK-SAME:   %[[ARG1:.+]]: tensor<?x?xf32>
//       CHECK:   %[[RESULT:.+]]:2 = linalg_ext.sort dimension(0)
//  CHECK-SAME:      outs(%[[ARG0]], %[[ARG1]]
//       CHECK:   return %[[RESULT]]#0, %[[RESULT]]#1

// -----

func @sort_multi_result_memref(
    %arg0: memref<?x?xi32>, %arg1: memref<?x?xf32>) {
  linalg_ext.sort dimension(0)
     outs(%arg0, %arg1 : memref<?x?xi32>, memref<?x?xf32>) {
     ^bb0(%arg2: i32, %arg3: i32, %arg4 : f32, %arg5 : f32):  // no predecessors
       %1 = arith.cmpf ogt, %arg4, %arg5 : f32
       linalg_ext.yield %1 : i1
     }
  return
}
// CHECK-LABEL: func @sort_multi_result_memref
//  CHECK-SAME:   %[[ARG0:.+]]: memref<?x?xi32>
//  CHECK-SAME:   %[[ARG1:.+]]: memref<?x?xf32>
//       CHECK:   linalg_ext.sort dimension(0)
//  CHECK-SAME:      outs(%[[ARG0]], %[[ARG1]]

// -----

func @scatter_tensor_dynamic(
    %original: tensor<?x?xf32>, %indices: tensor<?x1xi32>,
    %update: tensor<?x?xf32>) -> tensor<?x?xf32> {
  %0 = linalg_ext.scatter
    ins(%update, %indices : tensor<?x?xf32>, tensor<?x1xi32>)
    outs(%original: tensor<?x?xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    } -> tensor<?x?xf32>
  return %0 : tensor<?x?xf32>
}
// CHECK-LABEL: func @scatter_tensor_dynamic(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: tensor<?x?xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: tensor<?x1xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: tensor<?x?xf32>
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return %[[RESULT]]

// -----

func @scatter_tensor_static(
    %original: tensor<128x3xf32>, %indices: tensor<48x1xi32>,
    %update: tensor<48x3xf32>) -> tensor<128x3xf32> {
  %0 = linalg_ext.scatter
    ins(%update, %indices : tensor<48x3xf32>, tensor<48x1xi32>)
    outs(%original: tensor<128x3xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    } -> tensor<128x3xf32>
  return %0 : tensor<128x3xf32>
}
// CHECK-LABEL: func @scatter_tensor_static(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: tensor<128x3xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: tensor<48x1xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: tensor<48x3xf32>
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return %[[RESULT]]

// -----

func @scatter_tensor_multi_index_depth(
    %original: tensor<1x128x3xf32>, %indices: tensor<48x2xi32>,
    %update: tensor<48x3xf32>) -> tensor<1x128x3xf32> {
  %0 = linalg_ext.scatter
    ins(%update, %indices : tensor<48x3xf32>, tensor<48x2xi32>)
    outs(%original: tensor<1x128x3xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    } -> tensor<1x128x3xf32>
  return %0 : tensor<1x128x3xf32>
}
// CHECK-LABEL: func @scatter_tensor_multi_index_depth(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: tensor<1x128x3xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: tensor<48x2xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: tensor<48x3xf32>
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return %[[RESULT]]

// -----

func @scatter_memref_dynamic(
    %original: memref<?x?xf32>, %indices: memref<?x1xi32>,
    %update: memref<?x?xf32>) {
  linalg_ext.scatter
    ins(%update, %indices : memref<?x?xf32>, memref<?x1xi32>)
    outs(%original: memref<?x?xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    }
  return
}
// CHECK-LABEL: func @scatter_memref_dynamic(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: memref<?x?xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: memref<?x1xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: memref<?x?xf32>
//       CHECK:   linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return

// -----

func @scatter_memref_static(
    %original: memref<128x3xf32>, %indices: memref<48x1xi32>,
    %update: memref<48x3xf32>) {
  linalg_ext.scatter
    ins(%update, %indices : memref<48x3xf32>, memref<48x1xi32>)
    outs(%original: memref<128x3xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    }
  return
}
// CHECK-LABEL: func @scatter_memref_static(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: memref<128x3xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: memref<48x1xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: memref<48x3xf32>
//       CHECK:   linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return

// -----

func @scatter_memref_multi_index_depth(
    %original: memref<1x128x3xf32>, %indices: memref<48x2xi32>,
    %update: memref<48x3xf32>) {
  linalg_ext.scatter
    ins(%update, %indices : memref<48x3xf32>, memref<48x2xi32>)
    outs(%original: memref<1x128x3xf32>) {
    ^bb0(%arg1: f32, %arg2: f32):
      %1 = arith.addf %arg1, %arg2 : f32
      linalg_ext.yield %1 : f32
    }
  return
}
// CHECK-LABEL: func @scatter_memref_multi_index_depth(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]: memref<1x128x3xf32>
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]: memref<48x2xi32>
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]: memref<48x3xf32>
//       CHECK:   linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : f32
//       CHECK:   return

// -----

func @scatter_update_scalar_1D(
    %original: tensor<8xi32>, %indices: tensor<3x1xi32>,
    %updates: tensor<3xi32>) -> tensor<8xi32> {
  %0 = linalg_ext.scatter
    ins(%updates, %indices : tensor<3xi32>, tensor<3x1xi32>)
    outs(%original : tensor<8xi32>)  {
    ^bb0(%arg0: i32, %arg1: i32):  // no predecessors
      linalg_ext.yield %arg0 : i32
    } -> tensor<8xi32>
  return %0 : tensor<8xi32>
}
// CHECK-LABEL: func @scatter_update_scalar_1D(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : i32
//       CHECK:   return %[[RESULT]]

// -----

func @scatter_update_scalar_2D(
    %original: tensor<4x3xi32>, %indices: tensor<3x2xi32>,
    %updates: tensor<3xi32>) -> tensor<4x3xi32> {
  %0 = linalg_ext.scatter
    ins(%updates, %indices : tensor<3xi32>, tensor<3x2xi32>)
    outs(%original : tensor<4x3xi32>)  {
    ^bb0(%arg0: i32, %arg1: i32):  // no predecessors
      linalg_ext.yield %arg0 : i32
    } -> tensor<4x3xi32>
  return %0 : tensor<4x3xi32>
}
// CHECK-LABEL: func @scatter_update_scalar_2D(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : i32
//       CHECK:   return %[[RESULT]]

// -----

func @scatter_update_slice_2D(
    %original: tensor<4x3xi32>, %indices: tensor<1x1xi32>,
    %updates: tensor<1x3xi32>) -> tensor<4x3xi32> {
  %0 = linalg_ext.scatter
    ins(%updates, %indices : tensor<1x3xi32>, tensor<1x1xi32>)
    outs(%original : tensor<4x3xi32>)  {
    ^bb0(%arg0: i32, %arg1: i32):  // no predecessors
      linalg_ext.yield %arg0 : i32
    } -> tensor<4x3xi32>
  return %0 : tensor<4x3xi32>
}
// CHECK-LABEL: func @scatter_update_slice_2D(
//  CHECK-SAME:   %[[ORIGINAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[INDICES:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[UPDATE:[a-zA-Z0-9_]+]]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.scatter
//  CHECK-SAME:     ins(%[[UPDATE]], %[[INDICES]]
//  CHECK-SAME:     outs(%[[ORIGINAL]]
//       CHECK:     linalg_ext.yield %{{.+}} : i32
//       CHECK:   return %[[RESULT]]

// -----

func @fft_tensor(%arg0: tensor<1024xf32>, %arg1: tensor<1024xf32>)
    -> (tensor<1024xf32>, tensor<1024xf32>) {
  %cst1 = arith.constant 1 : index
  %0:2 = linalg_ext.fft
    ins(%cst1: index)
    outs(%arg0, %arg1: tensor<1024xf32>, tensor<1024xf32>)
  : tensor<1024xf32>, tensor<1024xf32>
  return %0#0, %0#1 : tensor<1024xf32>, tensor<1024xf32>
}
// CHECK-LABEL: func @fft_tensor(
//  CHECK-SAME:   %[[REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[IMAG:[a-zA-Z0-9_]+]]
//       CHECK:   %[[CST:.+]] = arith.constant 1 : index
//       CHECK:   %[[RES:.+]]:2 = linalg_ext.fft
//  CHECK-SAME:     ins(%[[CST]] : index)
//  CHECK-SAME:    outs(%[[REAL]], %[[IMAG]] : tensor<1024xf32>, tensor<1024xf32>)
//  CHECK-SAME:   : tensor<1024xf32>, tensor<1024xf32>
//       CHECK:   return %[[RES]]#0, %[[RES]]#1

// -----

func @fft_memref(%arg0: memref<1024xf32>, %arg1: memref<1024xf32>) {
  %cst1 = arith.constant 1 : index
  linalg_ext.fft
    ins(%cst1: index)
    outs(%arg0, %arg1: memref<1024xf32>, memref<1024xf32>)
  return
}
// CHECK-LABEL: func @fft_memref(
//  CHECK-SAME:   %[[REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[IMAG:[a-zA-Z0-9_]+]]
//       CHECK:   %[[CST:.+]] = arith.constant 1 : index
//       CHECK:   linalg_ext.fft
//  CHECK-SAME:     ins(%[[CST]] : index)
//  CHECK-SAME:    outs(%[[REAL]], %[[IMAG]] : memref<1024xf32>, memref<1024xf32>)
//       CHECK:   return

// -----

func @fft_tensor_coef(%arg0: tensor<1024xf32>, %arg1: tensor<1024xf32>,
    %arg2: tensor<1xf32>, %arg3: tensor<1xf32>) -> (tensor<1024xf32>, tensor<1024xf32>) {
  %cst1 = arith.constant 1 : index
  %0:2 = linalg_ext.fft
    ins(%cst1, %arg2, %arg3: index, tensor<1xf32>, tensor<1xf32>)
    outs(%arg0, %arg1: tensor<1024xf32>, tensor<1024xf32>)
  : tensor<1024xf32>, tensor<1024xf32>
  return %0#0, %0#1 : tensor<1024xf32>, tensor<1024xf32>
}
// CHECK-LABEL: func @fft_tensor_coef(
//  CHECK-SAME:   %[[REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[IMAG:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_IMAG:[a-zA-Z0-9_]+]]
//       CHECK:   %[[CST:.+]] = arith.constant 1 : index
//       CHECK:   %[[RES:.+]]:2 = linalg_ext.fft
//  CHECK-SAME:     ins(%[[CST]], %[[COEF_REAL]], %[[COEF_IMAG]] : index, tensor<1xf32>, tensor<1xf32>)
//  CHECK-SAME:    outs(%[[REAL]], %[[IMAG]] : tensor<1024xf32>, tensor<1024xf32>)
//  CHECK-SAME:   : tensor<1024xf32>, tensor<1024xf32>
//       CHECK:   return %[[RES]]#0, %[[RES]]#1

// -----

func @fft_memref_coef(%arg0: memref<1024xf32>, %arg1: memref<1024xf32>,
                 %arg2: memref<1xf32>, %arg3: memref<1xf32>) {
  %cst1 = arith.constant 1 : index
  linalg_ext.fft
    ins(%cst1, %arg2, %arg3: index, memref<1xf32>, memref<1xf32>)
    outs(%arg0, %arg1: memref<1024xf32>, memref<1024xf32>)
  return
}
// CHECK-LABEL: func @fft_memref_coef(
//  CHECK-SAME:   %[[REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[IMAG:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_IMAG:[a-zA-Z0-9_]+]]
//       CHECK:   %[[CST:.+]] = arith.constant 1 : index
//       CHECK:   linalg_ext.fft
//  CHECK-SAME:     ins(%[[CST]], %[[COEF_REAL]], %[[COEF_IMAG]] : index, memref<1xf32>, memref<1xf32>)
//  CHECK-SAME:    outs(%[[REAL]], %[[IMAG]] : memref<1024xf32>, memref<1024xf32>)
//       CHECK:   return

// -----

// The size of coefficient tensor is 2^(stage-1).
func @fft_tensor_coef_stage_5(%arg0: tensor<1024xf32>, %arg1: tensor<1024xf32>,
    %arg2: tensor<16xf32>, %arg3: tensor<16xf32>) -> (tensor<1024xf32>, tensor<1024xf32>) {
  %cst1 = arith.constant 5 : index
  %0:2 = linalg_ext.fft
    ins(%cst1, %arg2, %arg3: index, tensor<16xf32>, tensor<16xf32>)
    outs(%arg0, %arg1: tensor<1024xf32>, tensor<1024xf32>)
  : tensor<1024xf32>, tensor<1024xf32>
  return %0#0, %0#1 : tensor<1024xf32>, tensor<1024xf32>
}
// CHECK-LABEL: func @fft_tensor_coef_stage_5(
//  CHECK-SAME:   %[[REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[IMAG:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_REAL:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   %[[COEF_IMAG:[a-zA-Z0-9_]+]]
//       CHECK:   %[[CST:.+]] = arith.constant 5 : index
//       CHECK:   %[[RES:.+]]:2 = linalg_ext.fft
//  CHECK-SAME:     ins(%[[CST]], %[[COEF_REAL]], %[[COEF_IMAG]] : index, tensor<16xf32>, tensor<16xf32>)
//  CHECK-SAME:    outs(%[[REAL]], %[[IMAG]] : tensor<1024xf32>, tensor<1024xf32>)
//  CHECK-SAME:   : tensor<1024xf32>, tensor<1024xf32>
//       CHECK:   return %[[RES]]#0, %[[RES]]#1

// -----

func @reverse_tensor(%arg0: tensor<3x5xi32>) -> tensor<3x5xi32> {
  %init = linalg.init_tensor [3, 5] : tensor<3x5xi32>
  %0 = linalg_ext.reverse
         dimensions(dense<0> : tensor<1xi64>)
         ins(%arg0 : tensor<3x5xi32>)
         outs(%init : tensor<3x5xi32>) : tensor<3x5xi32>
  return %0 : tensor<3x5xi32>
}
// CHECK-LABEL: func @reverse_tensor
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9]+]]: tensor<3x5xi32>
//       CHECK:   %[[INIT:.+]] = linalg.init_tensor [3, 5]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.reverse
//  CHECK-SAME:      dimensions(dense<0> : tensor<1xi64>)
//  CHECK-SAME:      ins(%[[ARG0]]
//  CHECK-SAME:      outs(%[[INIT]]

// -----

func @reverse_memref(%arg0: memref<3x5xi32>, %arg1: memref<3x5xi32>) {
  linalg_ext.reverse
    dimensions(dense<0> : tensor<1xi64>)
    ins(%arg0 : memref<3x5xi32>)
    outs(%arg1 : memref<3x5xi32>)
  return
}
// CHECK-LABEL: func @reverse_memref
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9]+]]: memref<3x5xi32>
//  CHECK-SAME:   %[[ARG1:[a-zA-Z0-9]+]]: memref<3x5xi32>
//       CHECK:   linalg_ext.reverse
//  CHECK-SAME:      dimensions(dense<0> : tensor<1xi64>)
//  CHECK-SAME:      ins(%[[ARG0]]
//  CHECK-SAME:      outs(%[[ARG1]]

// -----

func @reverse_dynamic_tensor(%arg0: tensor<?x?xi32>) -> tensor<?x?xi32> {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %d0 = tensor.dim %arg0, %c0 : tensor<?x?xi32>
  %d1 = tensor.dim %arg0, %c1 : tensor<?x?xi32>
  %init = linalg.init_tensor [%d0, %d1] : tensor<?x?xi32>
  %0 = linalg_ext.reverse
         dimensions(dense<1> : tensor<1xi64>)
         ins(%arg0 : tensor<?x?xi32>)
         outs(%init : tensor<?x?xi32>) : tensor<?x?xi32>
  return %0 : tensor<?x?xi32>
}
// CHECK-LABEL: func @reverse_dynamic_tensor
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9]+]]: tensor<?x?xi32>
//   CHECK-DAG:   %[[C0:.+]] = arith.constant 0 : index
//   CHECK-DAG:   %[[C1:.+]] = arith.constant 1 : index
//   CHECK-DAG:   %[[D0:.+]] = tensor.dim %[[ARG0]], %[[C0]]
//   CHECK-DAG:   %[[D1:.+]] = tensor.dim %[[ARG0]], %[[C1]]
//       CHECK:   %[[INIT:.+]] = linalg.init_tensor [%[[D0]], %[[D1]]]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.reverse
//  CHECK-SAME:      dimensions(dense<1> : tensor<1xi64>)
//  CHECK-SAME:      ins(%[[ARG0]]
//  CHECK-SAME:      outs(%[[INIT]]

// -----

func @reverse_static_dynamic_tensor(%arg0: tensor<3x5xi32>) -> tensor<?x?xi32> {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %d0 = tensor.dim %arg0, %c0 : tensor<3x5xi32>
  %d1 = tensor.dim %arg0, %c1 : tensor<3x5xi32>
  %init = linalg.init_tensor [%d0, %d1] : tensor<?x?xi32>
  %0 = linalg_ext.reverse
         dimensions(dense<1> : tensor<1xi64>)
         ins(%arg0 : tensor<3x5xi32>)
         outs(%init : tensor<?x?xi32>) : tensor<?x?xi32>
  return %0 : tensor<?x?xi32>
}
// CHECK-LABEL: func @reverse_static_dynamic_tensor
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9]+]]: tensor<3x5xi32>
//   CHECK-DAG:   %[[C0:.+]] = arith.constant 0 : index
//   CHECK-DAG:   %[[C1:.+]] = arith.constant 1 : index
//   CHECK-DAG:   %[[D0:.+]] = tensor.dim %[[ARG0]], %[[C0]]
//   CHECK-DAG:   %[[D1:.+]] = tensor.dim %[[ARG0]], %[[C1]]
//       CHECK:   %[[INIT:.+]] = linalg.init_tensor [%[[D0]], %[[D1]]]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.reverse
//  CHECK-SAME:      dimensions(dense<1> : tensor<1xi64>)
//  CHECK-SAME:      ins(%[[ARG0]]
//  CHECK-SAME:      outs(%[[INIT]]

// -----

func @reverse_multi_dims(%arg0: tensor<3x5xi32>) -> tensor<3x5xi32> {
  %init = linalg.init_tensor [3, 5] : tensor<3x5xi32>
  %0 = linalg_ext.reverse
         dimensions(dense<[0, 1]> : tensor<2xi64>)
         ins(%arg0 : tensor<3x5xi32>)
         outs(%init : tensor<3x5xi32>) : tensor<3x5xi32>
  return %0 : tensor<3x5xi32>
}
// CHECK-LABEL: func @reverse_multi_dims
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9]+]]: tensor<3x5xi32>
//       CHECK:   %[[INIT:.+]] = linalg.init_tensor [3, 5]
//       CHECK:   %[[RESULT:.+]] = linalg_ext.reverse
//  CHECK-SAME:      dimensions(dense<[0, 1]> : tensor<2xi64>)
//  CHECK-SAME:      ins(%[[ARG0]]
//  CHECK-SAME:      outs(%[[INIT]]
