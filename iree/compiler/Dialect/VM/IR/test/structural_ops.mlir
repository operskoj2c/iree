// RUN: iree-opt -split-input-file %s | iree-opt -split-input-file | FileCheck %s

// CHECK-LABEL: @module_empty
vm.module @module_empty {}

// -----

// CHECK-LABEL: @module_attributed attributes {a}
vm.module @module_attributed attributes {a} {
  // CHECK: vm.func @fn()
  vm.func @fn()
}

// -----

// CHECK-LABEL: @module_structure
vm.module @module_structure {
  // CHECK-NEXT: vm.global.i32 public @g0 : i32
  vm.global.i32 @g0 : i32
  // CHECK-NEXT: vm.export @fn
  vm.export @fn
  // CHECK-NEXT: vm.func @fn
  vm.func @fn(%arg0 : i32) -> i32 {
    vm.return %arg0 : i32
  }

  // CHECK-LABEL: vm.func @fn_attributed(%arg0: i32) -> i32
  // CHECK: attributes {a}
  vm.func @fn_attributed(%arg0 : i32) -> i32
      attributes {a} {
    vm.return %arg0 : i32
  }
}

// -----

// CHECK-LABEL: @export_funcs
vm.module @export_funcs {
  // CHECK-NEXT: vm.export @fn
  vm.export @fn
  // CHECK-NEXT: vm.export @fn as("fn_alias")
  vm.export @fn as("fn_alias")
  // CHECK-NEXT: vm.func @fn()
  vm.func @fn() {
    vm.return
  }

  // CHECK-LABEL: vm.export @fn as("fn_attributed") attributes {a}
  vm.export @fn as("fn_attributed") attributes {a}
}

// -----

// CHECK-LABEL: @import_funcs
vm.module @import_funcs {
  // CHECK-NEXT: vm.import @my.fn_empty()
  vm.import @my.fn_empty()

  // CHECK-NEXT: vm.import @my.fn(%foo : i32, %bar : i32) -> i32
  vm.import @my.fn(%foo : i32, %bar : i32) -> i32

  // CHECK-NEXT: vm.import @my.fn_attrs(%foo : i32, %bar : i32) -> i32 attributes {a}
  vm.import @my.fn_attrs(%foo : i32, %bar : i32) -> i32 attributes {a}

  // CHECK-NEXT: vm.import @my.fn_varargs(%foo : vector<3xi32> ..., %bar : tuple<i32, i32> ...) -> i32
  vm.import @my.fn_varargs(%foo : vector<3xi32> ..., %bar : tuple<i32, i32> ...) -> i32
}

// -----

// CHECK-LABEL: @initializers
vm.module @initializers {
  // CHECK-NEXT: vm.initializer {
  // CHECK-NEXT:   vm.return
  // CHECK-NEXT: }
  vm.initializer {
    vm.return
  }

  // CHECK-NEXT: vm.initializer attributes {foo} {
  // CHECK-NEXT:   vm.return
  // CHECK-NEXT: }
  vm.initializer attributes {foo} {
    vm.return
  }

  // CHECK-NEXT: vm.initializer {
  vm.initializer {
    // CHECK-NEXT: %zero = vm.const.i32 0
    %zero = vm.const.i32 0
    // CHECK-NEXT:   vm.br ^bb1(%zero : i32)
    vm.br ^bb1(%zero: i32)
    // CHECK-NEXT: ^bb1(%0: i32):
  ^bb1(%0: i32):
    // CHECK-NEXT:   vm.return
    vm.return
  }
}
