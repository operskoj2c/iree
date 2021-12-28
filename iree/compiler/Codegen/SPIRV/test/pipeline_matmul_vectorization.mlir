// RUN: iree-opt -split-input-file -pass-pipeline='hal.executable(hal.executable.variant(iree-codegen-linalg-to-spirv-pipeline))' %s | IreeFileCheck %s

#executable_layout = #hal.executable.layout<push_constants = 0, sets = [
  #hal.descriptor_set.layout<0, bindings = [
    #hal.descriptor_set.binding<0, storage_buffer>,
    #hal.descriptor_set.binding<1, storage_buffer>,
    #hal.descriptor_set.binding<2, storage_buffer>
  ]>
]>
hal.executable private @fuse_and_vectorize_fill_matmul {
  hal.executable.variant @vulkan_spirv_fb, target = <"vulkan", "vulkan-spirv-fb", {
      spv.target_env = #spv.target_env<#spv.vce<v1.4, [Shader], []>, ARM:IntegratedGPU, {
        max_compute_shared_memory_size = 32768 : i32,
        max_compute_workgroup_invocations = 512 : i32,
        max_compute_workgroup_size = dense<512> : vector<3xi32>,
       subgroup_size = 16 : i32}>
    }> {
    hal.executable.entry_point @fuse_and_vectorize_fill_matmul layout(#executable_layout)
    builtin.module {
      func @fuse_and_vectorize_fill_matmul() {
        %c0 = arith.constant 0 : index
        %cst = arith.constant 0.000000e+00 : f32
        %c4096 = arith.constant 4096 : index
        %0 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(0) : !flow.dispatch.tensor<readonly:4096x4096xf32>
        %1 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(1) : !flow.dispatch.tensor<readonly:4096x4096xf32>
        %2 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(2) : !flow.dispatch.tensor<writeonly:4096x4096xf32>
        %workgroup_size_x = hal.interface.workgroup.size[0] : index
        %workgroup_size_y = hal.interface.workgroup.size[1] : index
        %workgroup_id_x = hal.interface.workgroup.id[0] : index
        %workgroup_count_x = hal.interface.workgroup.count[0] : index
        %workgroup_id_y = hal.interface.workgroup.id[1] : index
        %workgroup_count_y = hal.interface.workgroup.count[1] : index
        %3 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_id_y, %workgroup_size_y]
        %4 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_count_y, %workgroup_size_y]
        scf.for %arg0 = %3 to %c4096 step %4 {
          %5 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_id_x, %workgroup_size_x]
          %6 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_count_x, %workgroup_size_x]
          scf.for %arg1 = %5 to %c4096 step %6 {
            %7 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 4096)>(%arg0)[%workgroup_size_y]
            %8 = flow.dispatch.tensor.load %0, offsets = [%arg0, 0], sizes = [%7, 4096], strides = [1, 1] : !flow.dispatch.tensor<readonly:4096x4096xf32> -> tensor<?x4096xf32>
            %9 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 4096)>(%arg1)[%workgroup_size_x]
            %10 = flow.dispatch.tensor.load %1, offsets = [0, %arg1], sizes = [4096, %9], strides = [1, 1] : !flow.dispatch.tensor<readonly:4096x4096xf32> -> tensor<4096x?xf32>
            %11 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 4096)>(%arg0)[%workgroup_size_y]
            %12 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 4096)>(%arg1)[%workgroup_size_x]
            %13 = affine.min affine_map<(d0)[s0] -> (-d0 + 4096, s0)>(%arg0)[%workgroup_size_y]
            %14 = affine.min affine_map<(d0)[s0] -> (-d0 + 4096, s0)>(%arg1)[%workgroup_size_x]
            %15 = linalg.init_tensor [%13, %14] : tensor<?x?xf32>
            %16 = linalg.fill(%cst, %15) : f32, tensor<?x?xf32> -> tensor<?x?xf32>
            %17 = linalg.matmul ins(%8, %10 : tensor<?x4096xf32>, tensor<4096x?xf32>) outs(%16 : tensor<?x?xf32>) -> tensor<?x?xf32>
            flow.dispatch.tensor.store %17, %2, offsets = [%arg0, %arg1], sizes = [%11, %12], strides = [1, 1] : tensor<?x?xf32> -> !flow.dispatch.tensor<writeonly:4096x4096xf32>
          }
        }
        return
      }
    }
  }
}

//    CHECK-LABEL: spv.func @fuse_and_vectorize_fill_matmul
//      CHECK-NOT:   spv.Store "StorageBuffer"
//      CHECK-NOT:   spv.Load "StorageBuffer"
//          CHECK:   spv.mlir.loop
//  CHECK-COUNT-8:   spv.Load "StorageBuffer" %{{.*}} : vector<4xf32>
// CHECK-COUNT-16:   spv.GLSL.Fma %{{.*}}, %{{.*}} : vector<4xf32>
//  CHECK-COUNT-4:   spv.Store "StorageBuffer" %{{.*}}, %{{.*}} : vector<4xf32>

// -----

#executable_layout = #hal.executable.layout<push_constants = 0, sets = [
  #hal.descriptor_set.layout<0, bindings = [
    #hal.descriptor_set.binding<0, storage_buffer>,
    #hal.descriptor_set.binding<1, storage_buffer>,
    #hal.descriptor_set.binding<2, storage_buffer>,
    #hal.descriptor_set.binding<3, storage_buffer>
  ]>
]>
hal.executable private @fuse_and_vectorize_matmul_add {
  hal.executable.variant @vulkan_spirv_fb, target = <"vulkan", "vulkan-spirv-fb", {
      spv.target_env = #spv.target_env<#spv.vce<v1.4, [Shader], []>, ARM:IntegratedGPU, {
        max_compute_shared_memory_size = 32768 : i32,
        max_compute_workgroup_invocations = 512 : i32,
        max_compute_workgroup_size = dense<512> : vector<3xi32>,
       subgroup_size = 16 : i32}>
    }> {
    hal.executable.entry_point @fuse_and_vectorize_matmul_add layout(#executable_layout)
    builtin.module {
      func @fuse_and_vectorize_matmul_add() {
        %c0 = arith.constant 0 : index
        %cst = arith.constant 0.000000e+00 : f32
        %c1024 = arith.constant 1024 : index
        %c256 = arith.constant 256 : index
        %0 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(0) : !flow.dispatch.tensor<readonly:1024x256xf32>
        %1 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(1) : !flow.dispatch.tensor<readonly:1024x512xf32>
        %2 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(2) : !flow.dispatch.tensor<readonly:512x256xf32>
        %3 = hal.interface.binding.subspan type(storage_buffer) set(0) binding(3) : !flow.dispatch.tensor<writeonly:1024x256xf32>
        %workgroup_size_x = hal.interface.workgroup.size[0] : index
        %workgroup_size_y = hal.interface.workgroup.size[1] : index
        %workgroup_id_x = hal.interface.workgroup.id[0] : index
        %workgroup_count_x = hal.interface.workgroup.count[0] : index
        %workgroup_id_y = hal.interface.workgroup.id[1] : index
        %workgroup_count_y = hal.interface.workgroup.count[1] : index
        %4 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_id_y, %workgroup_size_y]
        %5 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_count_y, %workgroup_size_y]
        scf.for %arg0 = %4 to %c1024 step %5 {
          %6 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_id_x, %workgroup_size_x]
          %7 = affine.apply affine_map<()[s0, s1] -> (s0 * s1)>()[%workgroup_count_x, %workgroup_size_x]
          scf.for %arg1 = %6 to %c256 step %7 {
            %8 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 1024)>(%arg0)[%workgroup_size_y]
            %9 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 256)>(%arg1)[%workgroup_size_x]
            %10 = flow.dispatch.tensor.load %0, offsets = [%arg0, %arg1], sizes = [%8, %9], strides = [1, 1] : !flow.dispatch.tensor<readonly:1024x256xf32> -> tensor<?x?xf32>
            %11 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 1024)>(%arg0)[%workgroup_size_y]
            %12 = affine.min affine_map<(d0)[s0] -> (s0, -d0 + 256)>(%arg1)[%workgroup_size_x]
            %13 = linalg.init_tensor [%11, %12] : tensor<?x?xf32>
            %14 = affine.min affine_map<(d0)[s0] -> (-d0 + 1024, s0)>(%arg0)[%workgroup_size_y]
            %15 = flow.dispatch.tensor.load %1, offsets = [%arg0, 0], sizes = [%14, 512], strides = [1, 1] : !flow.dispatch.tensor<readonly:1024x512xf32> -> tensor<?x512xf32>
            %16 = affine.min affine_map<(d0)[s0] -> (-d0 + 256, s0)>(%arg1)[%workgroup_size_x]
            %17 = flow.dispatch.tensor.load %2, offsets = [0, %arg1], sizes = [512, %16], strides = [1, 1] : !flow.dispatch.tensor<readonly:512x256xf32> -> tensor<512x?xf32>
            %18 = affine.min affine_map<(d0)[s0] -> (-d0 + 1024, s0)>(%arg0)[%workgroup_size_y]
            %19 = affine.min affine_map<(d0)[s0] -> (-d0 + 256, s0)>(%arg1)[%workgroup_size_x]
            %20 = linalg.init_tensor [%18, %19] : tensor<?x?xf32>
            %21 = linalg.fill(%cst, %20) : f32, tensor<?x?xf32> -> tensor<?x?xf32>
            %22 = linalg.matmul ins(%15, %17 : tensor<?x512xf32>, tensor<512x?xf32>) outs(%21 : tensor<?x?xf32>) -> tensor<?x?xf32>
            %23 = linalg.generic {indexing_maps = [affine_map<(d0, d1) -> (d0, d1)>, affine_map<(d0, d1) -> (d0, d1)>, affine_map<(d0, d1) -> (d0, d1)>], iterator_types = ["parallel", "parallel"]} ins(%22, %10 : tensor<?x?xf32>, tensor<?x?xf32>) outs(%13 : tensor<?x?xf32>) {
            ^bb0(%arg2: f32, %arg3: f32, %arg4: f32):  // no predecessors
              %24 = arith.addf %arg2, %arg3 : f32
              linalg.yield %24 : f32
            } -> tensor<?x?xf32>
            flow.dispatch.tensor.store %23, %3, offsets = [%arg0, %arg1], sizes = [%11, %12], strides = [1, 1] : tensor<?x?xf32> -> !flow.dispatch.tensor<writeonly:1024x256xf32>
          }
        }
        return
      }
    }
  }
}

//    CHECK-LABEL: spv.func @fuse_and_vectorize_matmul_add
//      CHECK-NOT:   spv.Store "StorageBuffer"
//      CHECK-NOT:   spv.Load "StorageBuffer"
//          CHECK:   spv.mlir.loop
//  CHECK-COUNT-8:     spv.Load "StorageBuffer" %{{.*}} : vector<4xf32>
// CHECK-COUNT-16:     spv.GLSL.Fma %{{.*}}, %{{.*}} : vector<4xf32>
//          CHECK:   spv.mlir.merge
//  CHECK-COUNT-4:   spv.Load "StorageBuffer" %{{.*}} : vector<4xf32>
//      CHECK-NOT:   spv.Load "StorageBuffer"
//      CHECK-NOT:   spv.Store "StorageBuffer"
//  CHECK-COUNT-4:   spv.FAdd %{{.*}}, %{{.*}} : vector<4xf32>
//  CHECK-COUNT-4:   spv.Store "StorageBuffer" %{{.*}}, %{{.*}} : vector<4xf32>
