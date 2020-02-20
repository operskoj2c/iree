// RUN: iree-tf-opt %s -pass-pipeline=convert-tf-to-tf_tensorlist -split-input-file -verify-diagnostics | IreeFileCheck %s

// TODO(silvasean): Handle interprocedural conversion.

// CHECK-LABEL: func @basic
func @basic(%arg0: tensor<f32>, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>, %index: tensor<i32>, %item: tensor<f32>) -> tensor<f32> {
  // CHECK-NEXT: [[LIST0:%.+]] = "tf_tensorlist.Reserve"(%arg2, %arg1) : (tensor<0xi32>, tensor<i32>) -> !tf_tensorlist.list
  // CHECK-NEXT: [[LIST1:%.+]] = "tf_tensorlist.SetItem"([[LIST0]], %arg3, %arg4) : (!tf_tensorlist.list, tensor<i32>, tensor<f32>) -> !tf_tensorlist.list
  // CHECK-NEXT: [[T:%.+]] = "tf_tensorlist.GetItem"([[LIST1]], %arg3, %arg2) : (!tf_tensorlist.list, tensor<i32>, tensor<0xi32>) -> tensor<f32>
  // CHECK-NEXT: return [[T]] : tensor<f32>
  %list0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  %list1 = "tf.TensorListSetItem"(%list0, %index, %item) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<f32>) -> tensor<!tf.variant<tensor<f32>>>
  %t = "tf.TensorListGetItem"(%list1, %index, %element_shape) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<0xi32>) -> tensor<f32>
  return %t : tensor<f32>
}

// CHECK-LABEL: func @control_flow_simple
func @control_flow_simple(%arg0: tensor<f32>, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>, %index: tensor<i32>, %item: tensor<f32>) {
  // CHECK-NEXT: tf_tensorlist.Reserve
  // CHECK-NEXT: br ^bb1({{%.+}} : !tf_tensorlist.list)
  %list0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  br ^bb1(%list0 : tensor<!tf.variant<tensor<f32>>>)
^bb1(%list1: tensor<!tf.variant<tensor<f32>>>):
  // CHECK-NEXT: ^bb1({{%.+}}: !tf_tensorlist.list):
  // CHECK-NEXT: br ^bb2({{%.+}} : !tf_tensorlist.list)
  br ^bb2(%list1 : tensor<!tf.variant<tensor<f32>>>)
^bb2(%list2: tensor<!tf.variant<tensor<f32>>>):
  // CHECK-NEXT: ^bb2({{%.+}}: !tf_tensorlist.list):
  // CHECK-NEXT: tf_tensorlist.SetItem
  %list3 = "tf.TensorListSetItem"(%list2, %index, %item) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<f32>) -> tensor<!tf.variant<tensor<f32>>>
  return
}

// CHECK-LABEL: func @control_flow_converge
func @control_flow_converge(%arg0: tensor<f32>, %pred: i1, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>, %index: tensor<i32>, %item: tensor<f32>) {
  // CHECK-NEXT: tf_tensorlist.Reserve
  // CHECK-NEXT: cond_br %arg1, ^bb1({{%.+}} : !tf_tensorlist.list), ^bb1({{%.+}} : !tf_tensorlist.list)
  %list0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  cond_br %pred, ^bb1(%list0 : tensor<!tf.variant<tensor<f32>>>), ^bb1(%list0 : tensor<!tf.variant<tensor<f32>>>)
^bb1(%list_arg: tensor<!tf.variant<tensor<f32>>>):
  // CHECK-NEXT: ^bb1({{%.+}}: !tf_tensorlist.list):
  // CHECK-NEXT: tf_tensorlist.SetItem
  %list1 = "tf.TensorListSetItem"(%list_arg, %index, %item) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<f32>) -> tensor<!tf.variant<tensor<f32>>>
  return
}

// CHECK-LABEL: func @control_flow_loop
func @control_flow_loop(%arg0: tensor<f32>, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>, %index: tensor<i32>, %item: tensor<f32>) {
  // CHECK-NEXT: tf_tensorlist.Reserve
  // CHECK-NEXT: br ^bb1({{%.+}} : !tf_tensorlist.list)
  %list0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  br ^bb1(%list0 : tensor<!tf.variant<tensor<f32>>>)
^bb1(%list_arg: tensor<!tf.variant<tensor<f32>>>):
  // CHECK-NEXT: ^bb1({{%.+}}: !tf_tensorlist.list):
  // CHECK-NEXT: tf_tensorlist.SetItem
  // CHECK-NEXT: br ^bb1({{%.+}} : !tf_tensorlist.list)
  %list1 = "tf.TensorListSetItem"(%list_arg, %index, %item) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<f32>) -> tensor<!tf.variant<tensor<f32>>>
  br ^bb1(%list1 : tensor<!tf.variant<tensor<f32>>>)
}

// CHECK-LABEL: func @casting
func @casting(%arg0: tensor<f32>, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>, %index: tensor<i32>, %item: tensor<f32>) {
  // CHECK-NEXT: tf_tensorlist.Reserve
  // CHECK-NEXT: br ^bb1({{%.+}} : !tf_tensorlist.list)
  %list0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  br ^bb1(%list0 : tensor<!tf.variant<tensor<f32>>>)
^bb1(%list_arg: tensor<!tf.variant<tensor<f32>>>):
  // CHECK-NEXT: ^bb1({{%.+}}: !tf_tensorlist.list):
  // CHECK-NEXT: tf_tensorlist.SetItem
  // No cast.
  // CHECK-NEXT: br ^bb1({{%.+}} : !tf_tensorlist.list)
  %list1 = "tf.TensorListSetItem"(%list_arg, %index, %item) : (tensor<!tf.variant<tensor<f32>>>, tensor<i32>, tensor<f32>) -> tensor<!tf.variant>
  %list_casted = "tf.Cast"(%list1) {Truncate=false} : (tensor<!tf.variant>) -> tensor<!tf.variant<tensor<f32>>>
  br ^bb1(%list_casted : tensor<!tf.variant<tensor<f32>>>)
}

// CHECK-LABEL: func @non_tensorlist_variant
func @non_tensorlist_variant() {
  // CHECK: "tf.ProducesSomeNonTensorListVariant"() : () -> tensor<!tf.variant>
  %0 = "tf.ProducesSomeNonTensorListVariant"() : () -> tensor<!tf.variant>
  // CHECK: "tf.ConsumesSomeNonTensorListVariant"(%0) : (tensor<!tf.variant>) -> ()
  "tf.ConsumesSomeNonTensorListVariant"(%0) : (tensor<!tf.variant>) -> ()
}

// -----

func @unknown_op(%arg0: tensor<f32>, %num_elements: tensor<i32>, %element_shape: tensor<0xi32>) {
  %0 = "tf.TensorListReserve"(%element_shape, %num_elements) : (tensor<0xi32>, tensor<i32>) -> tensor<!tf.variant<tensor<f32>>>
  // expected-error@+1 {{unable to convert tensorlist op: unknown_dialect.unknown_op}}
  "unknown_dialect.unknown_op"(%0) : (tensor<!tf.variant<tensor<f32>>>) -> ()
}
