// RUN: iree-opt -iree-codegen-convert-to-gpu -canonicalize -cse -split-input-file %s | IreeFileCheck %s

#map0 = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>
module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3,
    [Shader], [SPV_KHR_storage_buffer_storage_class]>,
    {max_compute_workgroup_invocations = 128 : i32,
     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @parallel_4D() {
    %arg0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 4 : i32} : memref<?x?x?x?xf32>
    %arg1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 9 : i32} : memref<?x?x?x?xf32>
    %arg2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 10 : i32} : memref<?x?x?x?xf32>
    linalg.generic {
       indexing_maps = [#map0, #map0, #map0],
       iterator_types = ["parallel", "parallel", "parallel", "parallel"]}
      ins(%arg0, %arg1 : memref<?x?x?x?xf32>, memref<?x?x?x?xf32>)
     outs(%arg2 : memref<?x?x?x?xf32>) {
    ^bb0(%arg3 : f32, %arg4 : f32, %arg5 : f32):
      %0 = addf %arg3, %arg4 : f32
      linalg.yield %0 : f32
    }
    return
  }
  func private @parallel_4D__num_workgroups__
    (!shapex.ranked_shape<[?,?,?,?]>, !shapex.ranked_shape<[?,?,?,?]>,
     !shapex.ranked_shape<[?,?,?,?]>) -> (index, index, index)
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
// CHECK-LABEL: func @parallel_4D
//  CHECK-SAME:   local_size = dense<[32, 1, 1]>
//   CHECK-DAG:     %[[C0:.+]] = constant 0 : index
//   CHECK-DAG:     %[[C1:.+]] = constant 1 : index
//   CHECK-DAG:     %[[C2:.+]] = constant 2 : index
//   CHECK-DAG:     %[[C3:.+]] = constant 3 : index
//   CHECK-DAG:     %[[UB0:.+]] = dim %{{.+}}, %[[C0]]
//   CHECK-DAG:     %[[UB1:.+]] = dim %{{.+}}, %[[C1]]
//   CHECK-DAG:     %[[UB2:.+]] = dim %{{.+}}, %[[C2]]
//   CHECK-DAG:     %[[UB3:.+]] = dim %{{.+}}, %[[C3]]
//       CHECK:     %[[T4:.+]] = muli %[[UB3]], %[[UB2]]
//       CHECK:     %[[T5:.+]] = muli %[[T4]], %[[UB1]]
//       CHECK:     %[[UB:.+]] = muli %[[T5]], %[[UB0]]
//   CHECK-DAG:     %[[BID:.+]] = "gpu.block_id"() {dimension = "x"}
//   CHECK-DAG:     %[[BDIM:.+]] = "gpu.block_dim"() {dimension = "x"}
//   CHECK-DAG:     %[[TID:.+]] = "gpu.thread_id"() {dimension = "x"}
//       CHECK:     %[[BOFFSET:.+]] = muli %[[BID]], %[[BDIM]]
//       CHECK:     %[[IV:.+]] = addi %[[BOFFSET]], %[[TID]]
//       CHECK:     %[[COND:.+]] = cmpi "slt", %[[IV]], %[[UB]]
//       CHECK:     scf.if %[[COND]]
//       CHECK:       %[[IV0:.+]] = divi_signed %[[IV]], %[[T5]]
//       CHECK:       %[[T14:.+]] = remi_signed %[[IV]], %[[T5]]
//       CHECK:       %[[IV1:.+]] = divi_signed %[[T14]], %[[T4]]
//       CHECK:       %[[T16:.+]] = remi_signed %[[T14]], %[[T4]]
//       CHECK:       %[[IV2:.+]] = divi_signed %[[T16]], %[[UB3]]
//       CHECK:       %[[IV3:.+]] = remi_signed %[[T16]], %[[UB3]]
//       CHECK:       load %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]
//       CHECK:       load %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]
//       CHECK:       store %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]

// -----

#map0 = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>
module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3,
    [Shader], [SPV_KHR_storage_buffer_storage_class]>,
    {max_compute_workgroup_invocations = 128 : i32,
     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @parallel_4D_static() attributes {hal.num_workgroups_fn = @parallel_4D_static__num_workgroups__} {
    %arg0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 0 : i32} : memref<3x4x5x6xf32>
    %arg1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 1 : i32} : memref<3x4x5x6xf32>
    %arg2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 2 : i32} : memref<3x4x5x6xf32>
    linalg.generic {
       indexing_maps = [#map0, #map0, #map0],
       iterator_types = ["parallel", "parallel", "parallel", "parallel"]}
      ins(%arg0, %arg1 : memref<3x4x5x6xf32>, memref<3x4x5x6xf32>)
     outs(%arg2 : memref<3x4x5x6xf32>) {
    ^bb0(%arg3 : f32, %arg4 : f32, %arg5 : f32):
      %0 = addf %arg3, %arg4 : f32
      linalg.yield %0 : f32
    }
    return
  }
  func private @parallel_4D_static__num_workgroups__
    (!shapex.ranked_shape<[3,4,5,6]>, !shapex.ranked_shape<[3,4,5,6]>,
     !shapex.ranked_shape<[3,4,5,6]>) -> (index, index, index)
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
// CHECK-LABEL: func @parallel_4D_static()
//  CHECK-SAME:   hal.num_workgroups_fn = @[[NUM_WORKGROUPS_FN:[a-zA-Z0-9_]+]]
//  CHECK-SAME:   local_size = dense<[32, 1, 1]>
//   CHECK-DAG:     %[[C360:.+]] = constant 360 : index
//   CHECK-DAG:     %[[C120:.+]] = constant 120 : index
//   CHECK-DAG:     %[[C30:.+]] = constant 30 : index
//   CHECK-DAG:     %[[C6:.+]] = constant 6 : index
//   CHECK-DAG:     %[[BID:.+]] = "gpu.block_id"() {dimension = "x"}
//   CHECK-DAG:     %[[BDIM:.+]] = "gpu.block_dim"() {dimension = "x"}
//   CHECK-DAG:     %[[TID:.+]] = "gpu.thread_id"() {dimension = "x"}
//       CHECK:     %[[BOFFSET:.+]] = muli %[[BID]], %[[BDIM]]
//       CHECK:     %[[IV:.+]] = addi %[[BOFFSET]], %[[TID]]
//       CHECK:     %[[COND:.+]] = cmpi "slt", %[[IV]], %[[C360]]
//       CHECK:     scf.if %[[COND]]
//       CHECK:       %[[IV0:.+]] = divi_signed %[[IV]], %[[C120]]
//       CHECK:       %[[T14:.+]] = remi_signed %[[IV]], %[[C120]]
//       CHECK:       %[[IV1:.+]] = divi_signed %[[T14]], %[[C30]]
//       CHECK:       %[[T16:.+]] = remi_signed %[[T14]], %[[C30]]
//       CHECK:       %[[IV2:.+]] = divi_signed %[[T16]], %[[C6]]
//       CHECK:       %[[IV3:.+]] = remi_signed %[[T16]], %[[C6]]
//       CHECK:       load %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]
//       CHECK:       load %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]
//       CHECK:       store %{{.+}}[%[[IV0]], %[[IV1]], %[[IV2]], %[[IV3]]]

//       CHECK: func private @[[NUM_WORKGROUPS_FN]]
//   CHECK-DAG:   %[[C1:.+]] = constant 1 : index
//   CHECK-DAG:   %[[C12:.+]] = constant 12 : index
//       CHECK:   return %[[C12]], %[[C1]], %[[C1]]
// -----

#map0 = affine_map<() -> ()>
#accesses = [#map0, #map0, #map0]
#trait = {
  indexing_maps = #accesses,
  iterator_types = []
}

module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3,
    [Shader], [SPV_KHR_storage_buffer_storage_class]>,
    {max_compute_workgroup_invocations = 128 : i32,
     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @scalar_add() attributes {hal.num_workgroups_fn = @scalar_add__num_workgroups__} {
    %arg0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 0 : i32} : memref<f32>
    %arg1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 1 : i32} : memref<f32>
    %arg2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 2 : i32} : memref<f32>
    linalg.generic #trait
      ins(%arg0, %arg1 : memref<f32>, memref<f32>)
     outs(%arg2 : memref<f32>) {
    ^bb0(%arg3 : f32, %arg4 : f32, %arg5 : f32):
      %0 = addf %arg3, %arg4 : f32
      linalg.yield %0 : f32
     }
     return
  }
  func private @scalar_add__num_workgroups__
    (!shapex.ranked_shape<[]>, !shapex.ranked_shape<[]>,
     !shapex.ranked_shape<[]>) -> (index, index, index)
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
// CHECK-LABEL: func @scalar_add()
//  CHECK-SAME:   hal.num_workgroups_fn = @[[NUM_WORKGROUPS_FN:[a-zA-Z0-9_]+]]
//       CHECK:     load
//  CHECK-NEXT:     load
//  CHECK-NEXT:     addf
//  CHECK-NEXT:     store
//  CHECK-NEXT:     return

//       CHECK: func private @[[NUM_WORKGROUPS_FN]]
//   CHECK-DAG:   %[[C1:.+]] = constant 1 : index
//       CHECK:   return %[[C1]], %[[C1]], %[[C1]]

// -----

module {
  func @reduce_sum() {
    %arg0 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg0, operand_result_index = 0 : i32} : memref<?x?x?xf32>
    %arg1 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@arg1, operand_result_index = 1 : i32} : memref<f32>
    %arg2 = iree.placeholder for "interace buffer"
      {binding = @legacy_io::@ret0, operand_result_index = 2 : i32} : memref<?xf32>
    linalg.indexed_generic {
       indexing_maps = [affine_map<(d0, d1, d2) -> (d0, d1, d2)>, affine_map<(d0, d1, d2) -> ()>,
                        affine_map<(d0, d1, d2) -> (d0)>],
       iterator_types = ["parallel", "parallel", "reduction"]}
      ins(%arg0, %arg1 : memref<?x?x?xf32>, memref<f32>)
     outs(%arg2 : memref<?xf32>) {
    ^bb0(%arg3: index, %arg4: index, %arg5: index,
         %arg6: f32, %arg7: f32, %arg8: f32):   // no predecessors
      %c0 = constant 0 : index
      %cst = constant true
      %0 = cmpi "eq", %arg5, %c0 : index
      %1 = and %cst, %0 : i1
      %2 = select %1, %arg7, %arg8 : f32
      %3 = addf %arg6, %2 : f32
      linalg.yield %3 : f32
    }
    return
  }
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
// CHECK-LABEL: func @reduce_sum
//  CHECK-SAME:   local_size = dense<[32, 1, 1]> : vector<3xi32>
//   CHECK-DAG:     %[[C0:.+]] = constant 0 : index
//   CHECK-DAG:     %[[C1:.+]] = constant 1 : index
//   CHECK-DAG:     %[[C2:.+]] = constant 2 : index
//       CHECK:     %[[UB0:.+]] = dim %{{.+}}, %[[C0]]
//       CHECK:     %[[UB1:.+]] = dim %{{.+}}, %[[C1]]
//       CHECK:     %[[UB2:.+]] = dim %{{.+}}, %[[C2]]
//       CHECK:     %[[UB:.+]] = muli %[[UB1]], %[[UB0]]
//       CHECK:     %[[COND:.+]] = cmpi "slt", %{{.+}}, %[[UB]]
//       CHECK:     scf.if %[[COND]]
//       CHECK:       %[[IV0:.+]] = divi_signed %{{.+}}, %[[UB1]]
//       CHECK:       %[[IV1:.+]] = remi_signed %{{.+}}, %[[UB1]]
//       CHECK:       scf.for %[[IV:.+]] = %{{.+}} to %[[UB2]]
//       CHECK:         %[[ISZERO:.+]] = cmpi "eq", %[[IV]], %[[C0]]

// -----

#map0 = affine_map<()[s0] -> (s0 * 8)>
#map1 = affine_map<()[s0, s1] -> (8, s1 - s0 * 8)>
#map2 = affine_map<(d0)[s0] -> (4, -d0 + s0)>
#map3 = affine_map<(d0, d1)[s0, s1] -> (d0 * s1 + s0 + d1)>
#map4 = affine_map<(d0, d1, d2) -> (d0, d2)>
#map5 = affine_map<(d0, d1, d2) -> (d2, d1)>
#map6 = affine_map<(d0, d1, d2) -> (d0, d1)>

module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
                    {max_compute_workgroup_invocations = 128 : i32,
                     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @matmul() {
    %arg0 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0} : memref<?x?xf32>
    %arg1 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1} : memref<?x?xf32>
    %arg2 = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0} : memref<?x?xf32>
    %c4 = constant 4 : index
    %c0 = constant 0 : index
    %c1 = constant 1 : index
    %0 = dim %arg0, %c1 : memref<?x?xf32>
    %1 = "gpu.block_id"() {dimension = "x"} : () -> index
    %2 = "gpu.block_id"() {dimension = "y"} : () -> index
    scf.for %arg3 = %c0 to %0 step %c4 {
      %3 = affine.apply #map0()[%2]
      %4 = dim %arg0, %c0 : memref<?x?xf32>
      %5 = affine.min #map1()[%2, %4]
      %6 = affine.min #map2(%arg3)[%0]
      %7 = subview %arg0[%3, %arg3] [%5, %6] [1, 1]  : memref<?x?xf32> to memref<?x?xf32, #map3>
      %8 = dim %arg1, %c0 : memref<?x?xf32>
      %9 = affine.min #map2(%arg3)[%8]
      %10 = affine.apply #map0()[%1]
      %11 = dim %arg1, %c1 : memref<?x?xf32>
      %12 = affine.min #map1()[%1, %11]
      %13 = subview %arg1[%arg3, %10] [%9, %12] [1, 1]  : memref<?x?xf32> to memref<?x?xf32, #map3>
      %14 = dim %arg2, %c0 : memref<?x?xf32>
      %15 = affine.min #map1()[%2, %14]
      %16 = dim %arg2, %c1 : memref<?x?xf32>
      %17 = affine.min #map1()[%1, %16]
      %18 = subview %arg2[%3, %10] [%15, %17] [1, 1]  : memref<?x?xf32> to memref<?x?xf32, #map3>
      linalg.matmul {__internal_linalg_transform__ = "workgroup"}
        ins(%7, %13 : memref<?x?xf32, #map3>, memref<?x?xf32, #map3>)
       outs(%18 : memref<?x?xf32, #map3>)
    }
    return
  }
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}

// CHECK-LABEL: func @matmul
//   CHECK-DAG:   %[[C0:.+]] = constant 0
//   CHECK-DAG:   %[[C1:.+]] = constant 1
//       CHECK:   scf.for
//   CHECK-DAG:     %[[TIDX:.+]] = "gpu.thread_id"() {dimension = "x"}
//   CHECK-DAG:     %[[TIDY:.+]] = "gpu.thread_id"() {dimension = "y"}
//       CHECK:     %[[INBOUNDY:.+]] = cmpi "slt", %[[TIDY]], %{{.*}}
//       CHECK:     %[[INBOUNDX:.+]] = cmpi "slt", %[[TIDX]], %{{.*}}
//       CHECK:     %[[COND:.+]] = and %[[INBOUNDY]], %[[INBOUNDX]]
//       CHECK:     scf.if %[[COND]]
//       CHECK:       scf.for %{{.+}} = %[[C0]] to %{{.*}} step %[[C1]]
//   CHECK-NOT:         linalg.matmul

// -----

#map0 = affine_map<()[s0] -> (s0 * 4)>
#map1 = affine_map<()[s0] -> (s0 * 32)>
#map2 = affine_map<(d0)[s0] -> (1, -d0 + s0)>
#map3 = affine_map<(d0)[s0, s1] -> (s0 + 4, -d0 + s1)>
#map4 = affine_map<(d0)[s0, s1] -> (s0 + 32, -d0 + s1)>
#map5 = affine_map<(d0, d1, d2, d3)[s0, s1, s2, s3] -> (d0 * s1 + s0 + d1 * s2 + d2 * s3 + d3)>
#map6 = affine_map<(d0)[s0] -> (4, -d0 + s0)>
#map7 = affine_map<(d0)[s0] -> (32, -d0 + s0)>


module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
                    {max_compute_workgroup_invocations = 128 : i32,
                     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @conv_no_padding() {
    %arg0 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0} : memref<?x?x?x?xf32>
    %arg1 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1} : memref<?x?x?x?xf32>
    %arg2 = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0} : memref<?x?x?x?xf32>
    %c2 = constant 2 : index
    %c0 = constant 0 : index
    %c3 = constant 3 : index
    %c1 = constant 1 : index
    %0 = dim %arg0, %c0 : memref<?x?x?x?xf32>
    %1 = dim %arg0, %c1 : memref<?x?x?x?xf32>
    %2 = dim %arg1, %c0 : memref<?x?x?x?xf32>
    %3 = dim %arg2, %c1 : memref<?x?x?x?xf32>
    %4 = dim %arg2, %c2 : memref<?x?x?x?xf32>
    %5 = "gpu.block_id"() {dimension = "x"} : () -> index
    %6 = "gpu.grid_dim"() {dimension = "x"} : () -> index
    %7 = "gpu.block_id"() {dimension = "y"} : () -> index
    %8 = "gpu.grid_dim"() {dimension = "y"} : () -> index
    %9 = "gpu.block_id"() {dimension = "z"} : () -> index
    %10 = "gpu.grid_dim"() {dimension = "z"} : () -> index
    %11 = affine.apply #map0()[%7]
    %12 = affine.apply #map0()[%8]
    %13 = affine.apply #map1()[%5]
    %14 = affine.apply #map1()[%6]
    scf.parallel (%arg3, %arg4, %arg5) = (%9, %11, %13) to (%2, %3, %4) step (%10, %12, %14) {
      %15 = affine.min #map2(%arg3)[%2]
      %16 = dim %arg1, %c1 : memref<?x?x?x?xf32>
      %17 = affine.min #map3(%arg4)[%0, %16]
      %18 = dim %arg1, %c2 : memref<?x?x?x?xf32>
      %19 = affine.min #map4(%arg5)[%1, %18]
      %20 = dim %arg1, %c3 : memref<?x?x?x?xf32>
      %21 = subview %arg1[%arg3, %arg4, %arg5, 0] [%15, %17, %19, %20] [1, 1, 1, 1]
              : memref<?x?x?x?xf32> to memref<?x?x?x?xf32, #map5>
      %22 = dim %arg2, %c0 : memref<?x?x?x?xf32>
      %23 = affine.min #map2(%arg3)[%22]
      %24 = affine.min #map6(%arg4)[%3]
      %25 = affine.min #map7(%arg5)[%4]
      %26 = dim %arg2, %c3 : memref<?x?x?x?xf32>
      %27 = subview %arg2[%arg3, %arg4, %arg5, 0] [%23, %24, %25, %26] [1, 1, 1, 1]
              : memref<?x?x?x?xf32> to memref<?x?x?x?xf32, #map5>
      linalg.conv(%arg0, %21, %27)
        {__internal_linalg_transform__ = "workgroup", dilations = [1, 1], strides = [1, 1]}
        : memref<?x?x?x?xf32>, memref<?x?x?x?xf32, #map5>, memref<?x?x?x?xf32, #map5>
      scf.yield
    }
    return
  }
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}
//   CHECK-DAG: #[[MAP0:.+]] = affine_map<()[s0] -> (s0 * 4)>
//   CHECK-DAG: #[[MAP1:.+]] = affine_map<()[s0] -> (s0 * 32)>
//       CHECK: func @conv_no_padding
//   CHECK-DAG:   %[[ARG0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0}
//   CHECK-DAG:   %[[ARG1:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1}
//   CHECK-DAG:   %[[RET0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0}
//   CHECK-DAG:   %[[C0:.+]] = constant 0
//   CHECK-DAG:   %[[C1:.+]] = constant 1
//   CHECK-DAG:   %[[C2:.+]] = constant 2
//   CHECK-DAG:   %[[N:.+]] = dim %[[ARG1]], %[[C0]]
//   CHECK-DAG:   %[[P:.+]] = dim %[[RET0]], %[[C1]]
//   CHECK-DAG:   %[[Q:.+]] = dim %[[RET0]], %[[C2]]
//   CHECK-DAG:   %[[BIDX:.+]] = "gpu.block_id"() {dimension = "x"}
//   CHECK-DAG:   %[[NBLOCKSX:.+]] = "gpu.grid_dim"() {dimension = "x"}
//   CHECK-DAG:   %[[BIDY:.+]] = "gpu.block_id"() {dimension = "y"}
//   CHECK-DAG:   %[[NBLOCKSY:.+]] = "gpu.grid_dim"() {dimension = "y"}
//   CHECK-DAG:   %[[BIDZ:.+]] = "gpu.block_id"() {dimension = "z"}
//   CHECK-DAG:   %[[NBLOCKSZ:.+]] = "gpu.grid_dim"() {dimension = "z"}
//       CHECK:   %[[BOFFSETY:.+]] = affine.apply #[[MAP0]]()[%[[BIDY]]]
//       CHECK:   %[[BSTEPY:.+]] = affine.apply #[[MAP0]]()[%[[NBLOCKSY]]]
//       CHECK:   %[[BOFFSETX:.+]] = affine.apply #[[MAP1]]()[%[[BIDX]]]
//       CHECK:   %[[BSTEPX:.+]] = affine.apply #[[MAP1]]()[%[[NBLOCKSX]]]
//       CHECK:   scf.for %[[IV3:.+]] = %[[BIDZ]] to %[[N]] step %[[NBLOCKSZ]]
//       CHECK:     scf.for %[[IV4:.+]] = %[[BOFFSETY]] to %[[P]] step %[[BSTEPY]]
//       CHECK:       scf.for %[[IV5:.+]] = %[[BOFFSETX]] to %[[Q]] step %[[BSTEPX]]
//       CHECK:         %[[SV1:.+]] = subview %[[ARG1]][%[[IV3]], %[[IV4]], %[[IV5]], 0]
//       CHECK:         %[[SV2:.+]] = subview %[[RET0]][%[[IV3]], %[[IV4]], %[[IV5]], 0]
//   CHECK-DAG:         %[[TIDX:.+]] = "gpu.thread_id"() {dimension = "x"}
//   CHECK-DAG:         %[[TIDY:.+]] = "gpu.thread_id"() {dimension = "y"}
//   CHECK-DAG:         %[[TIDZ:.+]] = "gpu.thread_id"() {dimension = "z"}
//       CHECK:         %[[C1:.+]] = cmpi "slt", %[[TIDZ]], %{{.*}}
//       CHECK:         %[[C2:.+]] = cmpi "slt", %[[TIDY]], %{{.*}}
//       CHECK:         %[[C3:.+]] = and %[[C1]], %[[C2]]
//       CHECK:         %[[C4:.+]] = cmpi "slt", %[[TIDX]], %{{.*}}
//       CHECK:         %[[COND:.+]] = and %[[C3]], %[[C4]]
//       CHECK:         scf.if %[[COND]]
//       CHECK:           scf.for
//       CHECK:             scf.for
//       CHECK:               scf.for
//       CHECK:                 scf.for
//   CHECK-NOT:                   linalg.conv

// -----

#map0 = affine_map<()[s0] -> (s0 * 4)>
#map1 = affine_map<()[s0] -> (s0 * 32)>
#map2 = affine_map<(d0)[s0, s1] -> (s0 + 4, -d0 + s1)>
#map3 = affine_map<(d0)[s0, s1] -> (s0 + 32, -d0 + s1)>
#map4 = affine_map<(d0, d1)[s0, s1] -> (d0 * s1 + s0 + d1)>
#map5 = affine_map<(d0)[s0] -> (4, -d0 + s0)>
#map6 = affine_map<(d0)[s0] -> (32, -d0 + s0)>


module attributes {
  spv.target_env =
    #spv.target_env<#spv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]>,
                    {max_compute_workgroup_invocations = 128 : i32,
                     max_compute_workgroup_size = dense<[128, 128, 64]> : vector<3xi32>}>} {
  func @pooling_no_padding() {
    %arg0 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0} : memref<?x?xf32>
    %arg1 = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1} : memref<?x?xf32>
    %arg2 = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0} : memref<?x?xf32>
    %c0 = constant 0 : index
    %c1 = constant 1 : index
    %0 = dim %arg1, %c0 : memref<?x?xf32>
    %1 = dim %arg1, %c1 : memref<?x?xf32>
    %2 = dim %arg2, %c0 : memref<?x?xf32>
    %3 = dim %arg2, %c1 : memref<?x?xf32>
    %4 = "gpu.block_id"() {dimension = "x"} : () -> index
    %5 = "gpu.grid_dim"() {dimension = "x"} : () -> index
    %6 = "gpu.block_id"() {dimension = "y"} : () -> index
    %7 = "gpu.grid_dim"() {dimension = "y"} : () -> index
    %8 = affine.apply #map0()[%6]
    %9 = affine.apply #map0()[%7]
    %10 = affine.apply #map1()[%4]
    %11 = affine.apply #map1()[%5]
    scf.parallel (%arg3, %arg4) = (%8, %10) to (%2, %3) step (%9, %11) {
      %12 = dim %arg0, %c0 : memref<?x?xf32>
      %13 = affine.min #map2(%arg3)[%0, %12]
      %14 = dim %arg0, %c1 : memref<?x?xf32>
      %15 = affine.min #map3(%arg4)[%1, %14]
      %16 = subview %arg0[%arg3, %arg4] [%13, %15] [1, 1]  : memref<?x?xf32> to memref<?x?xf32, #map4>
      %17 = affine.min #map5(%arg3)[%2]
      %18 = affine.min #map6(%arg4)[%3]
      %19 = subview %arg2[%arg3, %arg4] [%17, %18] [1, 1]  : memref<?x?xf32> to memref<?x?xf32, #map4>
      linalg.pooling_max(%16, %arg1, %19)
        {__internal_linalg_transform__ = "workgroup", dilations = [1, 1], strides = [1, 1]}
        : memref<?x?xf32, #map4>, memref<?x?xf32>, memref<?x?xf32, #map4>
      scf.yield
    }
    return
  }
  hal.interface @legacy_io attributes {sym_visibility = "private"} {
    hal.interface.binding @arg0, set=0, binding=0, type="StorageBuffer", access="Read"
    hal.interface.binding @arg1, set=0, binding=1, type="StorageBuffer", access="Read"
    hal.interface.binding @ret0, set=0, binding=2, type="StorageBuffer", access="Write"
  }
}

//   CHECK-DAG: #[[MAP0:.+]] = affine_map<()[s0] -> (s0 * 4)>
//   CHECK-DAG: #[[MAP1:.+]] = affine_map<()[s0] -> (s0 * 32)>
//       CHECK: func @pooling_no_padding
//   CHECK-DAG:   %[[ARG0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg0}
//   CHECK-DAG:   %[[ARG1:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@arg1}
//   CHECK-DAG:   %[[RET0:.+]] = iree.placeholder for "interace buffer" {binding = @legacy_io::@ret0}
//   CHECK-DAG:   %[[C0:.+]] = constant 0 : index
//   CHECK-DAG:   %[[C1:.+]] = constant 1 : index
//   CHECK-DAG:   %[[P:.+]] = dim %[[RET0]], %[[C0]]
//   CHECK-DAG:   %[[Q:.+]] = dim %[[RET0]], %[[C1]]
//   CHECK-DAG:   %[[BIDX:.+]] = "gpu.block_id"() {dimension = "x"}
//   CHECK-DAG:   %[[NBLOCKSX:.+]] = "gpu.grid_dim"() {dimension = "x"}
//   CHECK-DAG:   %[[BIDY:.+]] = "gpu.block_id"() {dimension = "y"}
//   CHECK-DAG:   %[[NBLOCKSY:.+]] = "gpu.grid_dim"() {dimension = "y"}
//       CHECK:   %[[BOFFSETY:.+]] = affine.apply #[[MAP0]]()[%[[BIDY]]]
//       CHECK:   %[[BSTEPY:.+]] = affine.apply #[[MAP0]]()[%[[NBLOCKSY]]]
//       CHECK:   %[[BOFFSETX:.+]] = affine.apply #[[MAP1]]()[%[[BIDX]]]
//       CHECK:   %[[BSTEPX:.+]] = affine.apply #[[MAP1]]()[%[[NBLOCKSX]]]
//       CHECK:   scf.for %[[IV3:.+]] = %[[BOFFSETY]] to %[[P]] step %[[BSTEPY]]
//       CHECK:     scf.for %[[IV4:.+]] = %[[BOFFSETX]] to %[[Q]] step %[[BSTEPX]]
//       CHECK:       %[[SV1:.+]] = subview %[[ARG0]][%[[IV3]], %[[IV4]]]
//       CHECK:       %[[SV2:.+]] = subview %[[RET0]][%[[IV3]], %[[IV4]]]
//   CHECK-DAG:       %[[TIDX:.+]] = "gpu.thread_id"() {dimension = "x"}
//   CHECK-DAG:       %[[TIDY:.+]] = "gpu.thread_id"() {dimension = "y"}
//       CHECK:       %[[C1:.+]] = cmpi "slt", %[[TIDY]], %{{.*}}
//       CHECK:       %[[C2:.+]] = cmpi "slt", %[[TIDX]], %{{.*}}
//       CHECK:       %[[COND:.+]] = and %[[C1]], %[[C2]]
//       CHECK:       scf.if %[[COND]]
//       CHECK:         scf.for
//       CHECK:           scf.for
//   CHECK-NOT:             linalg.pooling_max
