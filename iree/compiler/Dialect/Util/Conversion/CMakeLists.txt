################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Dialect/Util/Conversion/BUILD                                  #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_cc_library(
  NAME
    Conversion
  HDRS
    "ConversionPatterns.h"
  SRCS
    "ConversionPatterns.cpp"
  DEPS
    LLVMSupport
    MLIRFunc
    MLIRIR
    MLIRSupport
    MLIRTransforms
    iree::compiler::Dialect::Util::IR
  PUBLIC
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
