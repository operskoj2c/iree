include(AddMLIRPython)

# Specifies that all MLIR packages are co-located under npcomp.
# TODO: Add an upstream cmake param for this vs having a global here.
add_compile_definitions("MLIR_PYTHON_PACKAGE_PREFIX=iree.compiler.")

################################################################################
# Sources
################################################################################

declare_mlir_python_sources(IREECompilerAPIPythonSources
  ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/iree/compiler"
  SOURCES
    transforms/ireec.py
)
declare_mlir_python_sources(IREECompilerAPIPythonExtensions)

declare_mlir_python_sources(IREECompilerAPIPythonTools
  ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/iree/compiler"
  SOURCES
    __init__.py
    tf.py
    tflite.py
    xla.py
  SOURCES_GLOB
    tools/*.py
)

################################################################################
# Extensions
################################################################################

declare_mlir_python_extension(IREECompilerAPIPythonExtensions.IREECTransforms
  MODULE_NAME _ireecTransforms
  ADD_TO_PARENT IREECompilerAPIPythonExtensions
  SOURCES
    IREECTransforms.cpp
  EMBED_CAPI_LINK_LIBS
    IREECompilerAPICompilerCAPI
  PRIVATE_LINK_LIBS
    LLVMSupport
)

################################################################################
# Generate packages and shared library
################################################################################

set(_source_components
  # Local sources.
  IREECompilerAPIPythonSources
  IREECompilerAPIPythonExtensions
  IREECompilerAPIPythonTools

  # TODO: Core is now implicitly building/registering all dialects, increasing
  # build burden by ~5x. Make it stop.
  MLIRPythonSources.Core

  # Passes should also be disaggregated.
  MLIRPythonSources.Passes

  # Core dialects (constrained to IREE input dialects).
  MLIRPythonSources.Dialects.arith
  MLIRPythonSources.Dialects.builtin
  MLIRPythonSources.Dialects.cf
  MLIRPythonSources.Dialects.func
  MLIRPythonSources.Dialects.linalg
  MLIRPythonSources.Dialects.math
  MLIRPythonSources.Dialects.memref
  MLIRPythonSources.Dialects.shape
  MLIRPythonSources.Dialects.tensor
  MLIRPythonSources.Dialects.tosa
  MLIRPythonSources.Dialects.vector

  # mhlo project.
  MLIRHLOPythonSources
  MLIRHLOPythonExtensions

  # iree-dialects project.
  IREEDialectsPythonSources
  IREEDialectsPythonExtensions
)

add_mlir_python_common_capi_library(IREECompilerAggregateCAPI
  INSTALL_COMPONENT IREECompilerPythonModules
  INSTALL_DESTINATION python_package/iree/compiler/_mlir_libs
  OUTPUT_DIRECTORY "${IREE_COMPILER_API_BINARY_DIR}/python_package/iree/compiler/_mlir_libs"
  RELATIVE_INSTALL_ROOT "../../../.."
  DECLARED_SOURCES ${_source_components}
)

add_mlir_python_modules(IREECompilerPythonModules
  ROOT_PREFIX "${IREE_COMPILER_API_BINARY_DIR}/python_package/iree/compiler"
  INSTALL_PREFIX "python_package/iree/compiler"
  DECLARED_SOURCES ${_source_components}
  COMMON_CAPI_LINK_LIBS
    IREECompilerAggregateCAPI
  )


################################################################################
# Tools linked against the shared CAPI library
################################################################################

function(add_iree_compiler_busybox_tool target)
  cmake_parse_arguments(ARG
    ""
    "OUTPUT_NAME"
    "SRCS"
    ${ARGN})

  add_executable(
    ${target}
    ${ARG_SRCS}
  )
  target_link_libraries(${target} IREECompilerAggregateCAPI)
  set_target_properties(${target}
    PROPERTIES
      OUTPUT_NAME "${ARG_OUTPUT_NAME}"
      RUNTIME_OUTPUT_DIRECTORY "${IREE_COMPILER_API_BINARY_DIR}/python_package/iree/compiler/_mlir_libs"
  )
  mlir_python_setup_extension_rpath(${target})
  add_dependencies(IREECompilerPythonModules ${target})
  install(TARGETS ${target}
    DESTINATION "python_package/iree/compiler/_mlir_libs"
  )
endfunction()

add_iree_compiler_busybox_tool(
  IREECompilerIREECompileTool
  OUTPUT_NAME iree-compile
  SRCS
    IREECompileTool.c
)

if(TARGET lld)
  add_iree_compiler_busybox_tool(
    IREECompilerLldTool
    OUTPUT_NAME iree-lld
    SRCS
      LldTool.c
  )
endif()
