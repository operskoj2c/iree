// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_CODEGEN_DIALECT_LOWERINGCONFIG
#define IREE_COMPILER_CODEGEN_DIALECT_LOWERINGCONFIG

include "iree/compiler/Codegen/Dialect/IREECodegenDialect.td"
include "mlir/IR/EnumAttr.td"

// List of pre-existing pipelines for translating executables.
def CPU_Default
    : I32EnumAttrCase<"CPUDefault", 0>;
def CPU_SingleTilingExpert
    : I32EnumAttrCase<"CPUSingleTilingExpert", 1>;
def CPU_DoubleTilingExpert
    : I32EnumAttrCase<"CPUDoubleTilingExpert", 2>;
def CPU_ConvTileAndDecomposeExpert
    : I32EnumAttrCase<"CPUConvTileAndDecomposeExpert", 3>;
def CPU_TileFuseAndVectorize
    : I32EnumAttrCase<"CPUTileFuseAndVectorize", 4>;
def CPU_BufferOpsTileAndVectorize
    : I32EnumAttrCase<"CPUBufferOpsTileAndVectorize", 5>;

def Linalg_TransformInterpCodegen
    : I32EnumAttrCase<"LinalgTransformInterpCodegen", 6>;

def LLVMGPU_SimpleDistribute
    : I32EnumAttrCase<"LLVMGPUDistribute", 7>;
def LLVMGPU_Vectorize
    : I32EnumAttrCase<"LLVMGPUVectorize", 8>;
def LLVMGPU_MatmulSimt
    : I32EnumAttrCase<"LLVMGPUMatmulSimt", 9>;
def LLVMGPU_MatmulTensorCore
    : I32EnumAttrCase<"LLVMGPUMatmulTensorCore", 10>;

def SPIRV_Distribute
    : I32EnumAttrCase<"SPIRVDistribute", 11>;
def SPIRV_Vectorize
    : I32EnumAttrCase<"SPIRVVectorize", 12>;
def SPIRV_VectorizeToCooperativeOps
    : I32EnumAttrCase<"SPIRVVectorizeToCooperativeOps", 13>;

def None
    : I32EnumAttrCase<"None", 0xff>;

// EnumAttrCase for all known lowerings for ops within dispatch region
// to scalar/native-vector code.
def DispatchLoweringPassPipelineEnum : I32EnumAttr<
    "DispatchLoweringPassPipeline",
    "identifier for pass pipeline use to lower dispatch region",
    [CPU_Default, CPU_SingleTilingExpert, CPU_DoubleTilingExpert,
     CPU_ConvTileAndDecomposeExpert, CPU_TileFuseAndVectorize,
     CPU_BufferOpsTileAndVectorize, Linalg_TransformInterpCodegen,
     LLVMGPU_SimpleDistribute, LLVMGPU_Vectorize, LLVMGPU_MatmulSimt,
     LLVMGPU_MatmulTensorCore, SPIRV_Distribute, SPIRV_Vectorize,
     SPIRV_VectorizeToCooperativeOps, None]> {
  let cppNamespace = "::mlir::iree_compiler::IREE::Codegen";
  // Don't generate a C++ class! We want to use the AttrDef
  let genSpecializedAttr = 0;
}

// Define the AttrDef
def DispatchLoweringPassPipelineAttr :
    EnumAttr<IREECodegen_Dialect, DispatchLoweringPassPipelineEnum, ""> {
  let assemblyFormat = "``$value";
}


def IREECodegen_TranslationInfoAttr :
    AttrDef<IREECodegen_Dialect, "TranslationInfo", []> {
  let mnemonic = "translation_info";
  let summary = [{drive dispatch entry point lowering}];
  let description = [{
    Specifies the information that is used to drive the translation of
    an entry point function using Linalg based structured-op
    lowering.. During executable translation this is attached to the
    `hal.executable.entry_point` operation.

    If this operation is already set on the root operation (as part of
    `iree_codegen.compilation_info`) that drives the compilation of a
    dispatch region (like `linalg.matmul`/`linalg.*conv*`), this
    attribute gets propagated to the entry point function.

    The fields are
    - `passPipeline` : The pass pipeline to use.
    - `workloadPerWorkgroup` : Specifies how much of the original
      `workload` is handled by a workgroup along `x`, `y` and `z`.  If
      left empty it implies that that there is a single workgroup that
      does the entire `workload`.

  }];

  let assemblyFormat = [{
    `<` `` $passPipeline (`,` `workload_per_wg` `=` $workloadPerWorkgroup^)? `>`
  }];

  let parameters = (ins
    AttrParameter<"IREE::Codegen::DispatchLoweringPassPipelineAttr",
        "Name of the pipeline to be invoked on the translation unit.">:$passPipeline,
    DefaultValuedParameter<"ArrayAttr", "ArrayAttr::get($_ctx, {})",
        "The workload mapped to a single workgroup">:$workloadPerWorkgroup
  );
  let builders = [
    AttrBuilder<(ins "DispatchLoweringPassPipeline":$passPipeline,
        CArg<"ArrayRef<int64_t>", "{}">:$workloadPerWorkgroup)>
  ];
  let extraClassDeclaration = [{
    // Returns the lowering pass pipeline set.
    DispatchLoweringPassPipeline getDispatchLoweringPassPipeline();

    // Returns values of the workloadPerWorkgroup field if set.
    SmallVector<int64_t> getWorkloadPerWorkgroupVals();
  }];
  let genVerifyDecl = 1;
}

def IREECodegen_LoweringConfigAttr :
    AttrDef<IREECodegen_Dialect, "LoweringConfig", []> {
  let mnemonic = "lowering_config";
  let summary = [{drive lowering of an operation within dispatch region}];
  let description = [{
    Specifies the information that is used by backend compiler to
    translate an operation to scalar code. The way the information is
    used is specific to each backend (indeed specific to the pass
    pipeline used) to compile that operation.

    TODO: Currently there is no verification that the configuration
    specifies everything needed for a pass-pipeline. The values to set
    for these parameters is dependent on the pass-pipeline
    implementation. In future, each pass pipeline could verify that
    the lowering configuration has all the necessary attributes for
    the pipeline.

  }];

  let assemblyFormat = [{
    `<` `tile_sizes` `=` $tileSizes
      (`,` `tile_interchange` `=` $tileInterchange^)?
      (`,` `native_vector_size` `=` $nativeVectorSize^)? `>`
  }];

  let parameters = (ins
    AttrParameter<"ArrayAttr",
        "The tile sizes to use for different levels of tiling">:$tileSizes,
    DefaultValuedParameter<"ArrayAttr", "ArrayAttr::get($_ctx, {})",
        "The tile interchange to use for different levels of tiling">:$tileInterchange,
    DefaultValuedParameter<"ArrayAttr", "ArrayAttr::get($_ctx, {})",
        "The native vector size to use for the given operation">:$nativeVectorSize
  );
  let builders = [
    AttrBuilder<(ins "TileSizesListTypeRef":$tileSizes,
        CArg<"TileSizesListTypeRef", "{}">:$tileInterchange,
        CArg<"ArrayRef<int64_t>", "{}">:$nativeVectorSize)>
  ];
  let extraClassDeclaration = [{
    // Returns the tile sizes for all levels set for the op.
    TileSizesListType getTileSizeVals();

    // Returns the tile sizes for a level set for the op.
    SmallVector<int64_t> getTileSizeVals(unsigned level);

    // Returns the tile interchange for a level set for the op.
    SmallVector<int64_t> getTileInterchangeVals(unsigned level);

    // Returns the native vector size to use.
    SmallVector<int64_t> getNativeVectorSizeVals();
  }];
  let genVerifyDecl = 1;
}

def IREECodegen_CompilationInfoAttr :
    AttrDef<IREECodegen_Dialect, "CompilationInfo", []> {
  let mnemonic = "compilation_info";
  let summary = [{drive lowering of an operation from input dialect}];
  let description = [{
    Specifies the information that allows controlling the compilation
    of operations like `linalg.matmul`/`linalg.*conv` within
    IREE. This information is used to override the defaults used by
    the IREE compiler. Currently it is only valid to set this on
    `linalg.matmul`/`linalg.*conv*` operations.

    TODO: It is expected that the `TranslationInfoAttr` and the
    `LoweringConfigAttr` are specified. Currently there is no
    verification that the values of the `LoweringConfigAttr` fully
    specifies the behaviour of the compilation path chosen with
    `TranslationInfoAttr`. This could be added in the future.  Note:
    Typically the values used for the first-level tiling in
    `LoweringConfigAttr` and `workload_per_wg` value in the
    `TranslationInfoAttr` are the same since the first-level of tile +
    distribute is already done at the `Flow` level. This verification
    is also a TODO.
  }];
  let parameters = (ins
    AttrParameter<"LoweringConfigAttr", "">:$loweringConfig,
    AttrParameter<"TranslationInfoAttr", "">:$translationInfo,
    DefaultValuedParameter<"ArrayAttr", "ArrayAttr::get($_ctx, {})",
        "The workgroup size to use during translation.">:$workgroupSize
  );

  let assemblyFormat = [{
    `<` `lowering_config` `=` $loweringConfig `,` `translation_info` `=` $translationInfo
    (`,` `workgroup_size` `=` $workgroupSize^)? `>`
  }];

  // These builders are externally for auto-tuner to generate the attribute.
  let builders = [
    AttrBuilder<(ins "TileSizesListTypeRef":$tileSizes,
      "TileSizesListTypeRef":$tileInterchange,
      CArg<"ArrayRef<int64_t>", "{}">:$nativeVectorSize,
      CArg<"ArrayRef<int64_t>", "{}">:$workgroupSize)>,
    AttrBuilder<(ins "TileSizesListTypeRef":$tileSizes,
      "TileSizesListTypeRef":$tileInterchange,
      "ArrayRef<int64_t>":$nativeVectorSize,
      "DispatchLoweringPassPipeline":$passPipeline,
      "ArrayRef<int64_t>":$workloadPerWorkgroup,
      CArg<"ArrayRef<int64_t>", "{}">:$workgroupSize)>,
    AttrBuilder<(ins "LoweringConfigAttr":$configAttr,
      "TranslationInfoAttr":$translationInfo,
      "ArrayRef<int64_t>":$workloadPerWorkgroup)>,
  ];
  let extraClassDeclaration = [{
    SmallVector<int64_t> getWorkgroupSizeVals();
  }];
  let genVerifyDecl = 1;
}

#endif // IREE_COMPILER_CODEGEN_DIALECT_LOWERINGCONFIG
