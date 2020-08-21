// RUN: iree-opt -split-input-file -verify-diagnostics -iree-flow-materialize-exported-reflection -iree-flow-merge-exported-reflection %s | IreeFileCheck %s

// -----
// CHECK-LABEL: func @notExported
// CHECK-NOT: iree.reflection
func @notExported(%arg0 : tensor<4x4xi64>) -> tensor<4x4xi64> {
  return %arg0 : tensor<4x4xi64>
}

// -----
// CHECK-LABEL: func @emptyWithVersion
// CHECK-SAME: iree.reflection = {f = "I1!R1!", fv = "1"}
func @emptyWithVersion() -> () attributes {iree.module.export}
{
  return
}

// -----
// CHECK-LABEL: func @exportedTensor
// CHECK-SAME: iree.reflection = {f = "I19!B7!t7d4d4B7!t7d5d5R10!B7!t7d5d5", fv = "1"}
func @exportedTensor(%arg0 : tensor<4x4xi64>, %arg1 : tensor<5x5xi64>) -> tensor<5x5xi64>
    attributes {iree.module.export}
{
  return %arg1 : tensor<5x5xi64>
}

// -----
// CHECK-LABEL: func @noReflectionOnAbiNone
// CHECK-NOT: iree.reflection
func @noReflectionOnAbiNone(%arg0 : tensor<4x4xi64>, %arg1 : tensor<5x5xi64>) -> tensor<5x5xi64>
    attributes {iree.module.export, iree.abi.none}
{
  return %arg1 : tensor<5x5xi64>
}

// -----
// CHECK-LABEL: @unsupportedTypeOnAbiNone
// Should not generate warning
func @unsupportedTypeOnAbiNone(%arg0 : i1) -> ()
    attributes {iree.module.export, iree.abi.none}
{
  return
}
// -----
// CHECK-LABEL: @reflectionOnBool
// CHECK-SAME: iree.reflection = {f = "I6!S3!t4R1!", fv = "1"}
func @reflectionOnBool(%arg0 : i1) -> ()
    attributes {iree.module.export}
{
  return
}

// -----
// expected-warning @+1 {{Argument #0 of function unsupportedType is not a recognized public ABI type and the function may not be invokable by standard tools}}
func @unsupportedType(%arg0 : i3) -> ()
    attributes {iree.module.export}
{
  return
}
