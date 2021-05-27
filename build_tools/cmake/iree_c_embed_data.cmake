# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(CMakeParseArguments)

# iree_c_embed_data()
#
# CMake function to imitate Bazel's c_embed_data rule.
#
# Parameters:
# NAME: Name of target (see Note).
# SRCS: List of source files to embed.
# GENERATED_SRCS: List of generated source files to embed.
# C_FILE_OUTPUT: The C implementation file to output.
# H_FILE_OUTPUT: The H header file to output.
# STRIP_PREFIX: Strips this verbatim prefix from filenames (in the TOC).
# FLATTEN: Removes all directory components from filenames (in the TOC).
# IDENTIFIER: The identifier to use in generated names (defaults to name).
# PUBLIC: Add this so that this library will be exported under ${PACKAGE}::
# Also in IDE, target will appear in ${PACKAGE} folder while non PUBLIC will be
# in ${PACKAGE}/internal.
# TESTONLY: When added, this target will only be built if user passes
#    -DIREE_BUILD_TESTS=ON to CMake.
# TODO(scotttodd): Support passing KWARGS down into iree_cc_library?
#
function(iree_c_embed_data)
  cmake_parse_arguments(
    _RULE
    "PUBLIC;TESTONLY;FLATTEN"
    "NAME;IDENTIFIER;STRIP_PREFIX;C_FILE_OUTPUT;H_FILE_OUTPUT"
    "SRCS;GENERATED_SRCS"
    ${ARGN}
  )

  if(_RULE_TESTONLY AND NOT IREE_BUILD_TESTS)
    return()
  endif()

  if(DEFINED _RULE_IDENTIFIER)
    set(_IDENTIFIER ${_RULE_IDENTIFIER})
  else()
    set(_IDENTIFIER ${_RULE_NAME})
  endif()

  set(_ARGS)
  list(APPEND _ARGS "--output_header=${_RULE_H_FILE_OUTPUT}")
  list(APPEND _ARGS "--output_impl=${_RULE_C_FILE_OUTPUT}")
  list(APPEND _ARGS "--identifier=${_IDENTIFIER}")
  if(DEFINED _RULE_STRIP_PREFIX})
    list(APPEND _ARGS "--strip_prefix=${_RULE_STRIP_PREFIX}")
  endif()
  if(${_RULE_FLATTEN})
    list(APPEND _ARGS "--flatten")
  endif()

  foreach(SRC ${_RULE_SRCS})
    list(APPEND _ARGS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
  endforeach(SRC)
  foreach(SRC ${_RULE_GENERATED_SRCS})
    list(APPEND _ARGS "${SRC}")
  endforeach(SRC)

  iree_get_executable_path(_EXE_PATH generate_embed_data)

  add_custom_command(
    OUTPUT "${_RULE_H_FILE_OUTPUT}" "${_RULE_C_FILE_OUTPUT}"
    COMMAND ${_EXE_PATH} ${_ARGS}
    DEPENDS ${_EXE_PATH} ${_RULE_SRCS} ${_RULE_GENERATED_SRCS}
  )

  if(_RULE_TESTONLY)
    set(_TESTONLY_ARG "TESTONLY")
  endif()
  if(_RULE_PUBLIC)
    set(_PUBLIC_ARG "PUBLIC")
  endif()

  iree_cc_library(
    NAME ${_RULE_NAME}
    HDRS "${_RULE_H_FILE_OUTPUT}"
    SRCS "${_RULE_C_FILE_OUTPUT}"
    "${_PUBLIC_ARG}"
    "${_TESTONLY_ARG}"
  )
endfunction()
