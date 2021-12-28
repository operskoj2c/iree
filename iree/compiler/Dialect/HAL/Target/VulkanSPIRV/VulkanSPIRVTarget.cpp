// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/Target/VulkanSPIRV/VulkanSPIRVTarget.h"

#include "iree/compiler/Codegen/Dialect/IREECodegenDialect.h"
#include "iree/compiler/Codegen/Passes.h"
#include "iree/compiler/Dialect/HAL/Target/TargetRegistry.h"
#include "iree/compiler/Dialect/Vulkan/IR/VulkanAttributes.h"
#include "iree/compiler/Dialect/Vulkan/IR/VulkanDialect.h"
#include "iree/compiler/Dialect/Vulkan/Utils/TargetEnvironment.h"
#include "iree/compiler/Utils/FlatbufferUtils.h"
#include "iree/schemas/spirv_executable_def_builder.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/IR/TargetAndABI.h"
#include "mlir/Dialect/SPIRV/Linking/ModuleCombiner.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Parser.h"
#include "mlir/Target/SPIRV/Serialization.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

namespace {
llvm::Optional<FileLineColLoc> findFirstFileLoc(Location baseLoc) {
  if (auto loc = baseLoc.dyn_cast<FusedLoc>()) {
    for (auto &childLoc : loc.getLocations()) {
      auto childResult = findFirstFileLoc(childLoc);
      if (childResult) return childResult;
    }
  } else if (auto loc = baseLoc.dyn_cast<FileLineColLoc>()) {
    return loc;
  }
  return llvm::None;
}

std::string guessModuleName(mlir::ModuleOp moduleOp) {
  std::string moduleName =
      moduleOp.getName().hasValue() ? moduleOp.getName().getValue().str() : "";
  if (!moduleName.empty()) return moduleName;
  auto loc = findFirstFileLoc(moduleOp.getLoc());
  if (loc.hasValue()) {
    return llvm::sys::path::stem(loc.getValue().getFilename()).str();
  } else {
    return "spirv_module";
  }
}
}  // namespace

VulkanSPIRVTargetOptions getVulkanSPIRVTargetOptionsFromFlags() {
  // TODO(antiagainst): Enable option categories once the following bug is
  // fixed: https://bugs.llvm.org/show_bug.cgi?id=44223 static
  // llvm::cl::OptionCategory halVulkanSPIRVOptionsCategory(
  //     "IREE Vulkan/SPIR-V backend options");

  static llvm::cl::opt<std::string> clVulkanTargetTriple(
      "iree-vulkan-target-triple", llvm::cl::desc("Vulkan target triple"),
      llvm::cl::init("cpu-swiftshader-unknown"));

  static llvm::cl::opt<std::string> clVulkanTargetEnv(
      "iree-vulkan-target-env",
      llvm::cl::desc(
          "Vulkan target environment as #vk.target_env attribute assembly"),
      llvm::cl::init(""));

  static llvm::cl::opt<bool> clVulkanKeepShaderModules(
      "iree-vulkan-keep-shader-modules",
      llvm::cl::desc("Save SPIR-V shader modules to disk separately"),
      llvm::cl::init(false));

  VulkanSPIRVTargetOptions targetOptions;
  targetOptions.vulkanTargetEnv = clVulkanTargetEnv;
  targetOptions.vulkanTargetTriple = clVulkanTargetTriple;
  targetOptions.keepShaderModules = clVulkanKeepShaderModules;

  return targetOptions;
}

// Returns the Vulkan target environment for conversion.
static spirv::TargetEnvAttr getSPIRVTargetEnv(
    const std::string &vulkanTargetEnv, const std::string &vulkanTargetTriple,
    MLIRContext *context) {
  if (!vulkanTargetEnv.empty()) {
    if (auto attr = mlir::parseAttribute(vulkanTargetEnv, context)) {
      if (auto vkTargetEnv = attr.dyn_cast<Vulkan::TargetEnvAttr>()) {
        return convertTargetEnv(vkTargetEnv);
      }
    }
    emitError(Builder(context).getUnknownLoc())
        << "cannot parse vulkan target environment as #vk.target_env "
           "attribute: '"
        << vulkanTargetEnv << "'";
  } else if (!vulkanTargetTriple.empty()) {
    return convertTargetEnv(
        Vulkan::getTargetEnvForTriple(context, vulkanTargetTriple));
  }

  return {};
}

class VulkanSPIRVTargetBackend : public TargetBackend {
 public:
  VulkanSPIRVTargetBackend(VulkanSPIRVTargetOptions options)
      : options_(std::move(options)) {}

  // NOTE: we could vary these based on the options such as 'vulkan-v1.1'.
  std::string name() const override { return "vulkan"; }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<IREE::Codegen::IREECodegenDialect, Vulkan::VulkanDialect,
                    spirv::SPIRVDialect, gpu::GPUDialect>();
  }

  IREE::HAL::DeviceTargetAttr getDefaultDeviceTarget(
      MLIRContext *context) const override {
    Builder b(context);
    SmallVector<NamedAttribute> configItems;

    // Picked from here to start:
    // https://vulkan.gpuinfo.org/displaydevicelimit.php?name=minStorageBufferOffsetAlignment&platform=android
    // https://vulkan.gpuinfo.org/displaydevicelimit.php?name=maxStorageBufferRange&platform=android
    // We should instead be querying the vulkan environment attributes.
    uint64_t maxAllocationSize = 1 * 1024 * 1024 * 1024ull;
    uint64_t minBufferOffsetAlignment = 256ull;
    uint64_t maxBufferRange = 128 * 1024 * 1024ull;
    uint64_t minBufferRangeAlignment = 16ull;
    configItems.emplace_back(
        b.getIdentifier("buffer_constraints"),
        BufferConstraintsAttr::get(b.getIndexAttr(maxAllocationSize),
                                   b.getIndexAttr(minBufferOffsetAlignment),
                                   b.getIndexAttr(maxBufferRange),
                                   b.getIndexAttr(minBufferRangeAlignment)));

    configItems.emplace_back(b.getIdentifier("executable_targets"),
                             getExecutableTargets(context));

    auto configAttr = b.getDictionaryAttr(configItems);
    return IREE::HAL::DeviceTargetAttr::get(
        context, b.getStringAttr(deviceID()), configAttr);
  }

  void buildTranslationPassPipeline(OpPassManager &passManager) override {
    buildSPIRVCodegenPassPipeline(passManager);
  }

  // TODO(antiagainst): Re-enable SPIR-V linking once the tensorflow integration
  // crash is fixed.
#if 0
  LogicalResult linkExecutables(mlir::ModuleOp moduleOp) override {
    // Note: Vulkan flavored SPIR-V does not have linking in the conventional
    // sense. For example, there is no cross-module symbol reference and symbol
    // resolution and such. It's more just combining all SPIR-V modules into the
    // one, with multiple entry points.

    // 1. Create source executable groups according to their executable
    // interface. We only combine executables in the same group.

    // Map from an executable interface's hash to all source executables having
    // that interface.
    llvm::DenseMap<llvm::hash_code, SmallVector<IREE::HAL::ExecutableOp, 4>>
        sourceExecutableOpGroups;

    int numExecutables = 0;
    for (auto op : moduleOp.getOps<IREE::HAL::ExecutableOp>()) {
      llvm::hash_code hash = interfaceOps.front().getInterfaceHash();
      sourceExecutableOpGroups[hash].push_back(op);

      ++numExecutables;
    }
    if (numExecutables <= 1) return success();

    SymbolTable symbolTable(moduleOp);

    auto sharedTargetsAttr = getExecutableTargets(moduleOp.getContext());
    if (llvm::size(sharedTargetsAttr) != 1) {
      return moduleOp.emitError("only one executable target is supported now");
    }

    auto sharedTargetAttr = sharedTargetsAttr.getValue()
                                .front()
                                .cast<IREE::HAL::ExecutableTargetAttr>();

    // Guess a module name, if needed, to make the output files readable.
    auto moduleName = guessModuleName(moduleOp);

    // 2. Create "linked" executables for each source executable group.
    // This just pulls in spv.module ops that should be combined into the same
    // hal.executable.variant inner module.

    SmallVector<mlir::ModuleOp, 8> innerModuleOps;
    innerModuleOps.reserve(sourceExecutableOpGroups.size());
    for (const auto &hashExecutablePair : sourceExecutableOpGroups) {
      llvm::hash_code hash = hashExecutablePair.first;
      const auto &sourceExecutableOps = hashExecutablePair.second;

      // Just one executable for this group. No need to link.
      if (sourceExecutableOps.size() == 1) continue;

      OpBuilder builder(moduleOp.getContext());

      // Create a new "linked" hal.executable for collecting all source
      // executables in this group.
      std::string linkedExecutableName =
          llvm::formatv("{0}_linked_{1}", moduleName, name());
      auto linkedExecutableOp = builder.create<IREE::HAL::ExecutableOp>(
          moduleOp.getLoc(), linkedExecutableName);
      symbolTable.insert(linkedExecutableOp, moduleOp.getBody()->begin());

      // Add our hal.executable.variant with an empty module.
      builder.setInsertionPointToStart(linkedExecutableOp.getBody());
      auto linkedTargetOp = builder.create<IREE::HAL::ExecutableVariantOp>(
          moduleOp.getLoc(), sharedTargetAttr.getSymbolNameFragment(),
          sharedTargetAttr);
      builder.setInsertionPoint(&linkedTargetOp.getBlock().back());
      innerModuleOps.push_back(
          builder.create<mlir::ModuleOp>(moduleOp.getLoc()));

      // Try linking together all executables in moduleOp.
      if (failed(linkExecutablesInto(
              moduleOp, sourceExecutableOps, linkedExecutableOp, linkedTargetOp,
              [](mlir::ModuleOp moduleOp) { return moduleOp; }, builder)))
        return failure();
    }

    // 3. Now we can have multiple spv.module ops in the same
    // hal.executable.variant inner module. Combining them into one.

    auto symbolRenameListener = [](spirv::ModuleOp symbolTable,
                                   StringRef oldSymbol, StringRef newSymbol) {
      // We don't care about global variable renaming. There should not exist
      // duplicated functions. But double check that.
      if (Operation *op = SymbolTable::lookupSymbolIn(symbolTable, oldSymbol)) {
        assert(!isa<spirv::FuncOp>(op) &&
               "found duplicated spv.func names when linking!");
      }
    };

    for (mlir::ModuleOp innerModule : innerModuleOps) {
      auto spvModules =
          llvm::to_vector<4>(innerModule.getBody()->getOps<spirv::ModuleOp>());
      if (spvModules.size() <= 1) continue;

      OpBuilder builder(innerModule);
      auto newModule = builder.create<mlir::ModuleOp>(innerModule.getLoc());

      // Create the combined spv.module op and erase the old inner module.
      builder.setInsertionPointToStart(newModule.getBody());
      spirv::combine(spvModules, builder, symbolRenameListener).release();
      innerModule.erase();
    }

    return success();
  }
#endif

  LogicalResult serializeExecutable(IREE::HAL::ExecutableVariantOp variantOp,
                                    OpBuilder &executableBuilder) override {
    ModuleOp innerModuleOp = variantOp.getInnerModule();
    auto spirvModuleOps = innerModuleOp.getOps<spirv::ModuleOp>();
    if (!llvm::hasSingleElement(spirvModuleOps)) {
      return variantOp.emitError()
             << "should only contain exactly one spv.module op";
    }
    auto spvModuleOp = *spirvModuleOps.begin();

    FlatbufferBuilder builder;
    iree_SpirVExecutableDef_start_as_root(builder);

    // Serialize the spirv::ModuleOp into the binary that we will embed in the
    // final flatbuffer.
    SmallVector<uint32_t, 256> spvBinary;
    if (failed(spirv::serialize(spvModuleOp, spvBinary)) || spvBinary.empty()) {
      return variantOp.emitError() << "failed to serialize spv.module";
    }
    auto spvCodeRef = flatbuffers_uint32_vec_create(builder, spvBinary.data(),
                                                    spvBinary.size());
    if (options_.keepShaderModules) {
      saveSpirvBinary(variantOp, spvBinary);
    }

    // The sequencer and runtime use ordinals instead of names. We provide the
    // list of entry point names here that are then passed in
    // VkShaderModuleCreateInfo.
    SmallVector<StringRef, 8> entryPointNames;
    spvModuleOp.walk([&](spirv::EntryPointOp entryPointOp) {
      entryPointNames.push_back(entryPointOp.fn());
    });
    auto entryPointsRef = builder.createStringVec(entryPointNames);

    iree_SpirVExecutableDef_entry_points_add(builder, entryPointsRef);
    iree_SpirVExecutableDef_code_add(builder, spvCodeRef);
    iree_SpirVExecutableDef_end_as_root(builder);

    // Add the binary data to the target executable.
    auto binaryOp = executableBuilder.create<IREE::HAL::ExecutableBinaryOp>(
        variantOp.getLoc(), variantOp.sym_name(),
        variantOp.target().getFormat(),
        builder.getBufferAttr(executableBuilder.getContext()));
    binaryOp.mime_typeAttr(
        executableBuilder.getStringAttr("application/x-flatbuffers"));

    return success();
  }

 private:
  ArrayAttr getExecutableTargets(MLIRContext *context) const {
    SmallVector<Attribute> targetAttrs;
    // If we had multiple target environments we would generate one target attr
    // per environment, with each setting its own environment attribute.
    targetAttrs.push_back(getExecutableTarget(
        context, getSPIRVTargetEnv(options_.vulkanTargetEnv,
                                   options_.vulkanTargetTriple, context)));
    return ArrayAttr::get(context, targetAttrs);
  }

  IREE::HAL::ExecutableTargetAttr getExecutableTarget(
      MLIRContext *context, spirv::TargetEnvAttr targetEnv) const {
    Builder b(context);
    SmallVector<NamedAttribute> configItems;

    configItems.emplace_back(b.getIdentifier(spirv::getTargetEnvAttrName()),
                             targetEnv);

    auto configAttr = b.getDictionaryAttr(configItems);
    return IREE::HAL::ExecutableTargetAttr::get(
        context, b.getStringAttr("vulkan"), b.getStringAttr("vulkan-spirv-fb"),
        configAttr);
  }

  void saveSpirvBinary(IREE::HAL::ExecutableVariantOp variantOp,
                       ArrayRef<uint32_t> binary) {
    llvm::SmallString<32> filePath;
    if (std::error_code error = llvm::sys::fs::createTemporaryFile(
            variantOp.getName(), "spv", filePath)) {
      llvm::errs() << "failed to generate file for SPIR-V binary: "
                   << error.message();
      return;
    }
    std::error_code error;
    auto file = std::make_unique<llvm::ToolOutputFile>(filePath, error,
                                                       llvm::sys::fs::OF_None);
    if (error) {
      llvm::errs() << "failed to open file for SPIR-V binary '" << filePath
                   << "': " << error.message();
      return;
    }

    mlir::emitRemark(variantOp.getLoc())
        << "SPIR-V binary for " << variantOp.getName() << " preserved:\n"
        << "    " << filePath;
    file->os().write(reinterpret_cast<const char *>(binary.data()),
                     binary.size() * sizeof(uint32_t));
    file->keep();
  }

  VulkanSPIRVTargetOptions options_;
};

void registerVulkanSPIRVTargetBackends(
    std::function<VulkanSPIRVTargetOptions()> queryOptions) {
  getVulkanSPIRVTargetOptionsFromFlags();
  auto backendFactory = [=]() {
    return std::make_shared<VulkanSPIRVTargetBackend>(queryOptions());
  };
  // #hal.device.target<"vulkan", ...
  static TargetBackendRegistration registration0("vulkan", backendFactory);
  // #hal.executable.target<"vulkan-spirv", ...
  static TargetBackendRegistration registration1("vulkan-spirv",
                                                 backendFactory);
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
