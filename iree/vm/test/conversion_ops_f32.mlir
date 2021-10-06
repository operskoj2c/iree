vm.module @conversion_ops_f32 {

  //===----------------------------------------------------------------------===//
  // Casting and type conversion/emulation
  //===----------------------------------------------------------------------===//

  // 5.5 f32 (0x40b00000 hex) -> 1085276160 int32
  vm.export @test_bitcast_i32_f32
  vm.func @test_bitcast_i32_f32() {
    %c1 = vm.const.i32 1085276160 : i32
    %c1dno = util.do_not_optimize(%c1) : i32
    %v = vm.bitcast.i32.f32 %c1dno : i32 -> f32
    %c2 = vm.const.f32 5.5 : f32
    vm.check.eq %v, %c2, "bitcast i32 to f32" : f32
    vm.return
  }

  // 1085276160 int32 (0x40b00000 hex) -> 5.5 f32
  vm.export @test_bitcast_f32_i32
  vm.func @test_bitcast_f32_i32() {
    %c1 = vm.const.f32 5.5 : f32
    %c1dno = util.do_not_optimize(%c1) : f32
    %v = vm.bitcast.f32.i32 %c1dno : f32 -> i32
    %c2 = vm.const.i32 1085276160 : i32
    vm.check.eq %v, %c2, "bitcast f32 to i32" : i32
    vm.return
  }

  vm.export @test_cast_si32_f32_int_max
  vm.func @test_cast_si32_f32_int_max() {
    %c1 = vm.const.i32 2147483647 : i32
    %c1dno = util.do_not_optimize(%c1) : i32
    %v = vm.cast.si32.f32 %c1dno : i32 -> f32
    %c2 = vm.const.f32 2147483647.0 : f32
    vm.check.eq %v, %c2, "cast signed integer to a floating-point value" : f32
    vm.return
  }

  vm.export @test_cast_si32_f32_int_min
  vm.func @test_cast_si32_f32_int_min() {
    %c1 = vm.const.i32 -2147483648 : i32
    %c1dno = util.do_not_optimize(%c1) : i32
    %v = vm.cast.si32.f32 %c1dno : i32 -> f32
    %c2 = vm.const.f32 -2147483648.0 : f32
    vm.check.eq %v, %c2, "cast signed integer to a floating-point value" : f32
    vm.return
  }

  vm.export @test_cast_ui32_f32_int_max
  vm.func @test_cast_ui32_f32_int_max() {
    %c1 = vm.const.i32 4294967295 : i32
    %c1dno = util.do_not_optimize(%c1) : i32
    %v = vm.cast.ui32.f32 %c1dno : i32 -> f32
    %c2 = vm.const.f32 4294967295.0 : f32
    vm.check.eq %v, %c2, "cast unsigned integer to a floating-point value" : f32
    vm.return
  }

  vm.export @test_cast_f32_si32_int_max
  vm.func @test_cast_f32_si32_int_max() {
    %c1 = vm.const.f32 2147483647.0 : f32
    %c1dno = util.do_not_optimize(%c1) : f32
    %v = vm.cast.f32.si32 %c1dno : f32 -> i32
    %c2 = vm.const.i32 -2147483648 : i32
    vm.check.eq %v, %c2, "cast floating-point value to a signed integer" : i32
    vm.return
  }

  vm.export @test_cast_f32_si32_int_min
  vm.func @test_cast_f32_si32_int_min() {
    %c1 = vm.const.f32 -2147483648.0 : f32
    %c1dno = util.do_not_optimize(%c1) : f32
    %v = vm.cast.f32.si32 %c1dno : f32 -> i32
    %c2 = vm.const.i32 -2147483648 : i32
    vm.check.eq %v, %c2, "cast floating-point value to a signed integer" : i32
    vm.return
  }

  vm.export @test_cast_f32_ui32_int_max
  vm.func @test_cast_f32_ui32_int_max() {
    %c1 = vm.const.f32 4294967295.0 : f32
    %c1dno = util.do_not_optimize(%c1) : f32
    %v = vm.cast.f32.ui32 %c1dno : f32 -> i32
    %c2 = vm.const.i32 0 : i32
    vm.check.eq %v, %c2, "cast floating-point value to an unsigned integer" : i32
    vm.return
  }

}
