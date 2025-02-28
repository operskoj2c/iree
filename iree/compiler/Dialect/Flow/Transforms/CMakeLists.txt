################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Dialect/Flow/Transforms/BUILD                                  #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_tablegen_library(
  NAME
    PassesIncGen
  TD_FILE
    "Passes.td"
  OUTS
    -gen-pass-decls Passes.h.inc
)

iree_cc_library(
  NAME
    Transforms
  HDRS
    "Passes.h"
    "Passes.h.inc"
  SRCS
    "CaptureDispatchDynamicDims.cpp"
    "CleanupNumericNarrowing.cpp"
    "CleanupTensorShapes.cpp"
    "ConvertConv2D1x1ToMatmulPass.cpp"
    "ConvertConv2DToImg2ColPass.cpp"
    "ConvertLinalgMatmulToMmt4D.cpp"
    "DeduplicateExecutables.cpp"
    "DispatchLinalgOnTensors.cpp"
    "ExpandTensorShapes.cpp"
    "ExportBenchmarkFuncs.cpp"
    "FusionOfTensorOps.cpp"
    "InferNumericNarrowing.cpp"
    "InitializeEmptyTensors.cpp"
    "InjectDispatchTracing.cpp"
    "InterchangeGenericOps.cpp"
    "OptimizeNumerics.cpp"
    "OutlineDispatchRegions.cpp"
    "PadLinalgOps.cpp"
    "PadTensorToSubTensorInsert.cpp"
    "PassDetail.h"
    "Passes.cpp"
    "SplitReduction.cpp"
    "StripAndSplatConstantVariables.cpp"
    "StripSignednessPass.cpp"
    "TestPartitionableLoopsInterface.cpp"
    "VerifyInputLegality.cpp"
  DEPS
    ::PassesIncGen
    IREELinalgExtDialect
    IREELinalgExtPasses
    LLVMSupport
    MLIRAffine
    MLIRArithmetic
    MLIRArithmeticUtils
    MLIRControlFlow
    MLIRFunc
    MLIRIR
    MLIRLinalg
    MLIRLinalgTransforms
    MLIRMemRef
    MLIRMemRefTransforms
    MLIRPass
    MLIRSCF
    MLIRSupport
    MLIRTensor
    MLIRTensorUtils
    MLIRTosa
    MLIRTransformUtils
    MLIRTransforms
    iree::compiler::Dialect::Flow::Conversion::TensorToFlow
    iree::compiler::Dialect::Flow::IR
    iree::compiler::Dialect::Flow::IR::PartitionableLoopsInterface
    iree::compiler::Dialect::HAL::IR
    iree::compiler::Dialect::Util::Analysis
    iree::compiler::Dialect::Util::Analysis::Attributes
    iree::compiler::Dialect::Util::Analysis::DFX
    iree::compiler::Dialect::Util::IR
    iree::compiler::Dialect::Util::Transforms
    iree::compiler::Utils
  PUBLIC
)

set_property(SOURCE
  DispatchLinalgOnTensors.cpp
  PROPERTY COMPILE_FLAGS $<$<CXX_COMPILER_ID:GNU>:-fno-devirtualize>)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
