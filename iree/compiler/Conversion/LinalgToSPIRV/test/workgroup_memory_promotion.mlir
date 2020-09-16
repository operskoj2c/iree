// RUN: iree-opt -split-input-file -iree-codegen-linalg-tile-and-fuse=use-workgroup-memory -canonicalize -cse %s | IreeFileCheck %s

module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
                    {max_compute_workgroup_invocations = 128 : i32,
                     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @matmul_tile()
    attributes {signature = (tensor<?x?xf32>, tensor<?x?xf32>) -> (tensor<?x?xf32>)} {
    %0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 0 : i32} : memref<?x?xf32>
    %1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 1 : i32} : memref<?x?xf32>
    %2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 2 : i32} : memref<?x?xf32>
    linalg.matmul %0, %1, %2 : (memref<?x?xf32>, memref<?x?xf32>, memref<?x?xf32>)
    return
  }
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
//       CHECK: func @matmul_tile()
//   CHECK-DAG:   %[[ARG0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0
//   CHECK-DAG:   %[[ARG1:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1
//   CHECK-DAG:   %[[RET0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0
//       CHECK:   scf.for
//       CHECK:     %[[ARG0SV:.+]] = subview %[[ARG0]]
//       CHECK:     %[[ARG1SV:.+]] = subview %[[ARG1]]
//       CHECK:     %[[RET0SV:.+]] = subview %[[RET0]]
//       CHECK:     %[[ALLOC1:.+]] = alloc() : memref<8x4xf32, 3>
//       CHECK:     %[[SUBVIEW1:.+]] = subview %[[ALLOC1]]
//       CHECK:     %[[ALLOC2:.+]] = alloc() : memref<4x8xf32, 3>
//       CHECK:     %[[SUBVIEW2:.+]] = subview %[[ALLOC2]]
//       CHECK:     linalg.copy(%[[ARG0SV]], %[[SUBVIEW1]])
//  CHECK-SAME:       "copy_to_workgroup_memory"
//       CHECK:     linalg.copy(%[[ARG1SV]], %[[SUBVIEW2]])
//  CHECK-SAME:       "copy_to_workgroup_memory"
//       CHECK:     linalg.matmul
//  CHECK-SAME:       "workgroup_memory_numprocs_ge_numiters"
//  CHECK-SAME:       %[[SUBVIEW1]], %[[SUBVIEW2]], %[[RET0SV]]
//   CHECK-DAG:     dealloc %[[ALLOC1]] : memref<8x4xf32, 3>
//   CHECK-DAG:     dealloc %[[ALLOC2]] : memref<4x8xf32, 3>

// -----

module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
                    {max_compute_workgroup_invocations = 128 : i32,
                     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @conv_no_padding_tile()
    attributes {signature = (tensor<3x4x3x2xf32>, tensor<?x?x?x3xf32>) -> (tensor<?x?x?x2xf32>)}  {
    %0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 0 : i32} : memref<3x4x3x2xf32>
    %1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 1 : i32} : memref<?x?x?x3xf32>
    %2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 2 : i32} : memref<?x?x?x2xf32>
    linalg.conv(%0, %1, %2) {dilations = [1, 1], strides = [1, 1]}
      : memref<3x4x3x2xf32>, memref<?x?x?x3xf32>, memref<?x?x?x2xf32>
    return
  }
}
//       CHECK: func @conv_no_padding_tile()
//   CHECK-DAG:   %[[ARG0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0
//   CHECK-DAG:   %[[ARG1:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1
//   CHECK-DAG:   %[[RET0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0
//       CHECK:   scf.parallel (%{{.*}}, %{{.*}}, %{{.*}})
//       CHECK:     %[[ARG1SV:.+]] = subview %[[ARG1]]
//       CHECK:     %[[RET0SV:.+]] = subview %[[RET0]]
//       CHECK:     %[[ALLOC1:.+]] = alloc() : memref<1x6x35x3xf32, 3>
//       CHECK:     %[[SUBVIEW1:.+]] = subview %[[ALLOC1]]
//       CHECK:     linalg.copy(%[[ARG1SV]], %[[SUBVIEW1]])
//  CHECK-SAME:        "copy_to_workgroup_memory"
//       CHECK:     linalg.conv(%[[ARG0]], %[[SUBVIEW1]], %[[RET0SV]])
//  CHECK-SAME:        "workgroup_memory"
//       CHECK:     dealloc %[[ALLOC1]] : memref<1x6x35x3xf32, 3>
