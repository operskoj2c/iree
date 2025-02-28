// RUN: iree-dialects-opt %s  -linalg-interp-transforms --split-input-file | FileCheck %s

// CHECK-DAG: #[[$MUL_MAP:.*]] = affine_map<(d0)[s0] -> (d0 * s0)>
// CHECK-DAG: #[[$SUB_MAP:.*]] = affine_map<(d0)[s0, s1] -> (-(d0 * s0) + s1, s0)>
// CHECK-DAG: #[[$ID1_MAP:.*]] = affine_map<(d0) -> (d0)>
#map0 = affine_map<(d0)[s0] -> (d0 ceildiv s0)>
#map1 = affine_map<(d0)[s0] -> (d0 * s0)>
#map2 = affine_map<(d0, d1) -> (d0 - d1)>
#map3 = affine_map<(d0, d1) -> (d0, d1)>
#map4 = affine_map<(d0) -> (d0)>

module {
  // CHECK-LABEL: func @static_tile
  //  CHECK-SAME:   %[[CHUNK_SIZE:[0-9a-z]+]]: index
  //  CHECK-SAME:   %[[IN:[0-9a-z]+]]: memref<?xf32>
  //  CHECK-SAME:   %[[OUT:[0-9a-z]+]]: memref<?xf32>
  func @static_tile(%arg0: index, %arg1: memref<?xf32>, %arg2: memref<?xf32>) {
    %cst = arith.constant 4.200000e+01 : f32
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %0 = memref.dim %arg2, %c0 : memref<?xf32>
    %1 = affine.apply #map0(%0)[%arg0]

    // CHECK: %[[M:.*]] = memref.dim %{{.*}}, %{{.*}} : memref<?xf32>
    // CHECK: %[[group:.*]] = async.create_group {{.*}}: !async.group 
    // CHECK: scf.for %[[IV:.*]] = {{.*}} 
    // CHECK:   %[[token:.*]] = async.execute {
    // CHECK:     subview
    // CHECK:     subview
    // CHECK:     linalg.generic
    // CHECK:     async.yield
    // CHECK:   }
    // CHECK:   async.add_to_group %[[token]], %[[group]] : !async.token
    // CHECK: }
    // CHECK: async.await_all %[[group]]
    iree_linalg_ext.in_parallel %1 -> () {
      ^bb0(%arg3: index):  // no predecessors
        %3 = affine.apply #map1(%arg3)[%arg0]
        %4 = affine.apply #map2(%0, %3)
        %5 = affine.min #map3(%4, %arg0)

        %6 = memref.subview %arg2[%3] [%5] [%c1] : memref<?xf32> to memref<?xf32, offset:?, strides:[?]>
        %7 = memref.subview %arg1[%3] [%5] [1] : memref<?xf32> to memref<?xf32, offset:?, strides:[?]>

        linalg.generic {indexing_maps = [#map4, #map4], iterator_types = ["parallel"]} 
          ins(%7 : memref<?xf32, offset:?, strides:[?]>) outs(%6 : memref<?xf32, offset:?, strides:[?]>) {
        ^bb0(%arg4: f32, %arg5: f32):  // no predecessors
          %9 = arith.mulf %arg4, %cst : f32
          linalg.yield %9 : f32
        }

        iree_linalg_ext.perform_concurrently {
        }
    }
    return
  }

  pdl.pattern @match_iree_linalg_ext_in_parallel : benefit(1) {
    %0 = operands
    %1 = types
    %2 = operation "iree_linalg_ext.in_parallel"(%0 : !pdl.range<value>)  -> (%1 : !pdl.range<type>)
    rewrite %2 with "iree_linalg_transform.apply"
  }
  iree_linalg_transform.sequence {
    %0 = match @match_iree_linalg_ext_in_parallel
    %1 = rewrite_iree_linalg_ext_in_parallel_to_async %0
  }
}
