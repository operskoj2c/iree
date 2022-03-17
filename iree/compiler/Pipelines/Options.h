// Copyright 2022 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_PIPELINES_OPTIONS_H_
#define IREE_COMPILER_PIPELINES_OPTIONS_H_

#include "iree/compiler/Utils/OptionUtils.h"

namespace mlir {
namespace iree_compiler {

struct BindingOptions {
  // Whether to include runtime support functions for the IREE native ABI.
  bool native = true;
  // Whether to include runtime support functions required for the IREE TFLite
  // API compatibility bindings.
  bool tflite = false;

  void bindOptions(OptionsBinder &binder);
  using FromFlags = OptionsFromFlags<BindingOptions>;
};

// The transformation to apply to the input prior to main compiler execution.
// These input pipelines are purposefully primitive and mainly focused on
// test case/reproducers as opposed to anything that should be coming from
// a user. For user/framework level interfacing, a dedicated importer likely
// needs to be created in order to represent whole-module level framework
// quirks. These are just about the ops in the functions.
struct InputDialectOptions {
  enum class Type {
    // Applies no input transformation. Only supported core and extension ops
    // are supported.
    none,
    // Legalizes input defined over TOSA ops.
    tosa,
    // Legalizes input defined over MHLO ops.
    mhlo,
    // Special case of 'mhlo' legalization which also performs some XLA
    // cleanup activities.
    xla,
  };
  Type type = Type::none;

  void bindOptions(OptionsBinder &binder);
  using FromFlags = OptionsFromFlags<InputDialectOptions>;
};

// Options controlling high level optimizations.
struct HighLevelOptimizationOptions {
  // Enables const-expr hoisting into globals.
  bool constExprHoisting = false;

  // Enables recursive evaluation of immutable globals using the compiler
  // and runtime.
  bool constEval = false;

  // Optimizations to reduce numeric precision where it is safe to do so.
  bool numericPrecisionReduction = false;

  // Strips debug assertions after any useful information has been extracted.
  bool stripAssertions = false;

  void bindOptions(OptionsBinder &binder);
  using FromFlags = OptionsFromFlags<HighLevelOptimizationOptions>;
};

// Options controlling scheduling across host/device.
struct SchedulingOptions {
  // TODO(benvanik): find a way to share this with
  // Stream/Transforms/PassDetail.h w/o circular deps.
  // Defines the output format of a dump pass.
  enum class DumpOutputFormat {
    // Dumping disabled.
    None = 0,
    // Human-readable pretty printing.
    Pretty = 1,
    // Pretty printing with additional information that can result in large
    // dumps.
    Verbose = 2,
    // Comma separated values for throwing into Sheets.
    CSV = 3,
    // JSON format for better structure and data exchange.
    JSON = 4,
  };
  // Enables and specifies the the format for a stream statistics dump.
  DumpOutputFormat dumpStatisticsFormat = DumpOutputFormat::None;
  // File path to write statistics to; or `` for stderr or `-` for stdout.
  std::string dumpStatisticsFile = "";

  // TODO(benvanik): favor size/speed/etc for partitioning.
  // TODO(benvanik): execution model to optimize for (unified/discrete memory,
  //                 single/multiple processors, etc).

  void bindOptions(OptionsBinder &binder);
  using FromFlags = OptionsFromFlags<SchedulingOptions>;
};

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_PIPELINES_OPTIONS_H_
