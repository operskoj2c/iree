// RUN: iree-opt -split-input-file -iree-flow-rematerialize-dispatch-constants %s | IreeFileCheck %s

// CHECK-LABEL: func @rematerializeSmall
func @rematerializeSmall(%arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  // CHECK: %[[WORKLOAD0:.+]] = constant 16 : index
  %cst = constant 16 : index
  %small = constant dense<1.23> : tensor<4x4xf32>
  // CHECK: %[[R0:.+]] = flow.dispatch.region[%[[WORKLOAD0]] : index](%arg1 = %arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  %0 = flow.dispatch.region[%cst : index](%arg1 = %arg0 : tensor<4x4xf32>, %arg2 = %small : tensor<4x4xf32>) -> tensor<4x4xf32> {
    // CHECK-NEXT: %[[REMAT_SMALL:.+]] = constant dense<1.230000e+00> : tensor<4x4xf32>
    // CHECK-NEXT: %1 = mhlo.add %arg1, %[[REMAT_SMALL]] : tensor<4x4xf32>
    %3 = mhlo.add %arg1, %arg2 : tensor<4x4xf32>
    flow.return %3 : tensor<4x4xf32>
  }
  return %0 : tensor<4x4xf32>
}

// -----

// CHECK-LABEL: func @rematerializeSplat
func @rematerializeSplat(%arg0 : tensor<1025xi8>) -> tensor<1025xi8> {
  // CHECK-DAG: %[[WORKLOAD0:.+]] = constant 16 : index
  %cst = constant 16 : index
  %large = constant dense<8> : tensor<1025xi8>
  // CHECK-NEXT: %[[R0:.+]] = flow.dispatch.region[%[[WORKLOAD0]] : index](%arg1 = %arg0 : tensor<1025xi8>) -> tensor<1025xi8> {
  %0 = flow.dispatch.region[%cst : index](%arg1 = %arg0 : tensor<1025xi8>, %arg2 = %large : tensor<1025xi8>) -> tensor<1025xi8> {
    // CHECK-NEXT: %[[REMAT_SPLAT:.+]] = constant dense<8> : tensor<1025xi8>
    // CHECK-NEXT: %1 = mhlo.add %arg1, %[[REMAT_SPLAT]] : tensor<1025xi8>
    %3 = mhlo.add %arg1, %arg2 : tensor<1025xi8>
    flow.return %3 : tensor<1025xi8>
  }
  return %0 : tensor<1025xi8>
}

// -----

// CHECK-LABEL: func @noRematerializeLarge
func @noRematerializeLarge(%arg0 : tensor<1025xi8>) -> tensor<1025xi8> {
  // CHECK-DAG: %[[WORKLOAD0:.+]] = constant 16 : index
  // CHECK-DAG: %[[CST:.+]] = constant dense<{{.+}}> : tensor<1025xi8>
  %cst = constant 16 : index
  %large = constant dense<[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255,0]> : tensor<1025xi8>
  // CHECK-NEXT: %[[R0:.+]] = flow.dispatch.region[%[[WORKLOAD0]] : index](%arg1 = %arg0 : tensor<1025xi8>, %arg2 = %[[CST]] : tensor<1025xi8>) -> tensor<1025xi8> {
  %0 = flow.dispatch.region[%cst : index](%arg1 = %arg0 : tensor<1025xi8>, %arg2 = %large : tensor<1025xi8>) -> tensor<1025xi8> {
    // CHECK-NEXT: %1 = mhlo.add %arg1, %arg2 : tensor<1025xi8>
    %3 = mhlo.add %arg1, %arg2 : tensor<1025xi8>
    flow.return %3 : tensor<1025xi8>
  }
  return %0 : tensor<1025xi8>
}

// -----

// CHECK-LABEL: func @noRematerializeIntoDot
func @noRematerializeIntoDot(%arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  // CHECK-DAG: %[[WORKLOAD0:.+]] = constant 16 : index
  // CHECK-DAG: %[[SMALL:.+]] = constant dense<1.230000e+00> : tensor<4x4xf32>
  %cst = constant 16 : index
  %small = constant dense<1.23> : tensor<4x4xf32>
  // CHECK-NEXT: %[[R0:.+]] = flow.dispatch.region[%[[WORKLOAD0]] : index](%arg1 = %arg0 : tensor<4x4xf32>, %arg2 = %[[SMALL]] : tensor<4x4xf32>) -> tensor<4x4xf32> {
  %0 = flow.dispatch.region[%cst : index](%arg1 = %arg0 : tensor<4x4xf32>, %arg2 = %small : tensor<4x4xf32>) -> tensor<4x4xf32> {
    // CHECK-NEXT: %1 = "mhlo.dot"(%arg1, %arg2) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
    %3 = "mhlo.dot"(%arg1, %arg2) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
    flow.return %3 : tensor<4x4xf32>
  }
  return %0 : tensor<4x4xf32>
}

// -----

func @constant_capture(%arg0: tensor<10x20xf32>) -> tensor<10x20xf32> {
  %c200 = constant 200 : index
  %cst = constant 1.000000e+00 : f32
  %cst_0 = constant dense<2.000000e+00> : tensor<10x20xf32>
  %cst_1 = constant dense<
    [1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00, 5.000000e+00,
     6.000000e+00, 7.000000e+00, 8.000000e+00, 9.000000e+00, 1.000000e+01]>
    : tensor<10xf32>
  %0 = flow.dispatch.region[%c200 : index]
    (%arg1 = %arg0 : tensor<10x20xf32>, %arg2 = %cst_0 : tensor<10x20xf32>,
     %arg3 = %cst_1 : tensor<10xf32>, %arg4 = %cst : f32) -> tensor<10x20xf32> {
    %1 = linalg.init_tensor [10, 20] : tensor<10x20xf32>
    %2 = linalg.generic
      {indexing_maps = [affine_map<(d0, d1) -> (d0, d1)>,
                        affine_map<(d0, d1) -> (d0, d1)>,
                        affine_map<(d0, d1) -> (d0)>,
                        affine_map<(d0, d1) -> (d0, d1)>],
       iterator_types = ["parallel", "parallel"]}
      ins(%arg1, %arg2, %arg3
        : tensor<10x20xf32>, tensor<10x20xf32>, tensor<10xf32>)
      outs(%1 : tensor<10x20xf32>) {
    ^bb0(%arg5: f32, %arg6: f32, %arg7: f32, %arg8: f32):  // no predecessors
      %3 = addf %arg5, %arg4 : f32
      %4 = mulf %3, %arg6 : f32
      %5 = addf %4, %arg7 : f32
      linalg.yield %5 : f32
    } -> tensor<10x20xf32>
    flow.return %2 : tensor<10x20xf32>
  }
  return %0 : tensor<10x20xf32>
}

// CHECK-LABEL: func @constant_capture
//  CHECK-SAME:   %[[ARG0:[a-zA-Z0-9_]+]]: tensor<10x20xf32>
//       CHECK:   %[[CST:.+]] = constant dense<[1.000000e+00, 2.000000e+00,
//  CHECK-SAME:     3.000000e+00, 4.000000e+00, 5.000000e+00, 6.000000e+00,
//  CHECK-SAME:     7.000000e+00, 8.000000e+00, 9.000000e+00, 1.000000e+01]>
//       CHECK:   flow.dispatch.region
//  CHECK-SAME:     %[[ARG1:[a-zA-Z0-9_]+]] = %[[ARG0]]
//  CHECK-SAME:     %[[ARG2:[a-zA-Z0-9_]+]] = %[[CST]]
//   CHECK-DAG:     %[[CST_0:.+]] = constant 1.000000e+00 : f32
//   CHECK-DAG:     %[[CST_1:.+]] = constant dense<2.000000e+00> : tensor<10x20xf32>
//       CHECK:     %[[RESULT:.+]] = linalg.generic
//  CHECK-SAME:       ins(%[[ARG1]], %[[CST_1]], %[[ARG2]]
//  CHECK-SAME:       ) {
//       CHECK:     ^{{[a-zA-Z0-9_]+}}(
//  CHECK-SAME:         %[[ARG3:.[a-zA-Z0-9_]+]]: f32,
//  CHECK-SAME:         %[[ARG4:.[a-zA-Z0-9_]+]]: f32,
//  CHECK-SAME:         %[[ARG5:.[a-zA-Z0-9_]+]]: f32,
//  CHECK-SAME:         %[[ARG6:.[a-zA-Z0-9_]+]]: f32)
//       CHECK:         %[[T0:.+]] = addf %[[ARG3]], %[[CST_0]]
//       CHECK:         %[[T1:.+]] = mulf %[[T0]], %[[ARG4]]
//       CHECK:         %[[T2:.+]] = addf %[[T1]], %[[ARG5]]
//       CHECK:         linalg.yield %[[T2]]
//       CHECK:       }
//       CHECK:     flow.return %[[RESULT]]

// -----

func @rematerialize_dispatch_workgroups(%arg0: tensor<8x8xf32>, %arg1: tensor<8x8xf32>) -> tensor<8x8xf32> {
  %cst_0 = constant 0.0 : f32
  %c2 = constant 1 : index
  %0 = flow.dispatch.workgroups[%c2, %c2, %c2] (%cst_0, %arg0, %arg1) : (f32, tensor<8x8xf32>, tensor<8x8xf32>) -> tensor<8x8xf32> = (%arg2 : f32, %arg3 : !flow.dispatch.input<8x8xf32>, %arg4 : !flow.dispatch.input<8x8xf32>, %arg5 : !flow.dispatch.output<8x8xf32>) {
    %c0 = constant 0 : index
    %c1 = constant 1 : index
    %c8 = constant 8 : index
    %1 = linalg.init_tensor [8, 8] : tensor<8x8xf32>
    %2 = linalg.fill(%1, %arg2) : tensor<8x8xf32>, f32 -> tensor<8x8xf32>
    %3 = flow.dispatch.input.load %arg3, offsets = [%c0, %c0], sizes = [%c8, %c8], strides = [%c1, %c1] : !flow.dispatch.input<8x8xf32> -> tensor<8x8xf32>
    %4 = flow.dispatch.input.load %arg4, offsets = [%c0, %c0], sizes = [%c8, %c8], strides = [%c1, %c1] : !flow.dispatch.input<8x8xf32> -> tensor<8x8xf32>
    %5 = linalg.matmul ins(%3, %4 : tensor<8x8xf32>, tensor<8x8xf32>) outs(%2 : tensor<8x8xf32>) -> tensor<8x8xf32>
    flow.dispatch.output.store %5, %arg5, offsets = [%c0, %c0], sizes = [%c8, %c8], strides = [%c1, %c1] : tensor<8x8xf32> -> !flow.dispatch.output<8x8xf32>
    flow.return
  }
  return %0: tensor<8x8xf32>
}

// CHECK: func @rematerialize_dispatch_workgroups(%[[ARG1:.+]]: tensor<8x8xf32>, %[[ARG2:.+]]: tensor<8x8xf32>)
// CHECK: %[[CONST1:.+]] = constant 1 : index
//       CHECK:   flow.dispatch.workgroups[%[[CONST1]], %[[CONST1]], %[[CONST1]]] (%[[ARG1]], %[[ARG2]])
//            CHECK: %[[CONST0:.+]] = constant 0.000000e+00 : f32
//            CHECK: %[[INIT_TENSOR:.+]] = linalg.init_tensor [8, 8] : tensor<8x8xf32>
//            CHECK:  linalg.fill(%[[INIT_TENSOR]], %[[CONST0]]) : tensor<8x8xf32>, f32 -> tensor<8x8xf32>
