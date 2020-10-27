// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iree/compiler/Dialect/HAL/Target/LLVM/LLVMBaseTarget.h"

#include "iree/compiler/Conversion/LinalgToLLVM/Passes.h"
#include "iree/compiler/Dialect/HAL/Target/LLVM/LLVMIRPasses.h"
#include "llvm/Support/FormatVariadic.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Linalg/IR/LinalgTypes.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/Vector/VectorOps.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

namespace {

// Destructively merges |sourceModuleOp| into |targetModuleOp|.
// |targetSymbolTable| is updated with the new symbols.
void mergeModuleInto(mlir::ModuleOp sourceModuleOp,
                     mlir::ModuleOp targetModuleOp,
                     DenseMap<StringRef, Operation *> &targetSymbolMap) {
  auto allOps = llvm::to_vector<8>(llvm::map_range(
      *sourceModuleOp.getBody(), [&](Operation &op) { return &op; }));
  for (auto &op : allOps) {
    if (op->isKnownTerminator()) continue;
    if (auto symbolInterface = dyn_cast<SymbolOpInterface>(op)) {
      if (targetSymbolMap.count(symbolInterface.getName())) {
        // TODO(scotttodd): compare ops to ensure we aren't copying different
        // things with the same name.
        continue;
      }
      targetSymbolMap[symbolInterface.getName()] = op;
    }
    op->moveBefore(&targetModuleOp.getBody()->back());
  }

  // Now that we're done cloning its ops, delete the original target op.
  sourceModuleOp.erase();
}

// Replaces each usage of an entry point with its original symbol name with a
// new symbol name.
void replaceEntryPointUses(mlir::ModuleOp moduleOp,
                           const DenseMap<Attribute, Attribute> &replacements) {
  for (auto funcOp : moduleOp.getOps<mlir::FuncOp>()) {
    funcOp.walk([&](IREE::HAL::CommandBufferDispatchSymbolOp dispatchOp) {
      auto it = replacements.find(dispatchOp.entry_point());
      if (it != replacements.end()) {
        dispatchOp.entry_pointAttr(it->second.cast<SymbolRefAttr>());
      }
    });
  }
}

}  // namespace

LLVMBaseTargetBackend::LLVMBaseTargetBackend(LLVMTargetOptions options)
    : options_(std::move(options)) {}

void LLVMBaseTargetBackend::getDependentDialects(
    DialectRegistry &registry) const {
  // clang-format off
    registry.insert<AffineDialect,
                    linalg::LinalgDialect,
                    LLVM::LLVMDialect,
                    scf::SCFDialect,
                    vector::VectorDialect>();
  // clang-format on
}

void LLVMBaseTargetBackend::buildTranslationPassPipeline(
    ExecutableTargetOp targetOp, OpPassManager &passManager) {
  buildLLVMTransformPassPipeline(passManager);
}

LogicalResult LLVMBaseTargetBackend::linkExecutables(mlir::ModuleOp moduleOp) {
  OpBuilder builder = OpBuilder::atBlockBegin(moduleOp.getBody());
  auto executableOps =
      llvm::to_vector<8>(moduleOp.getOps<IREE::HAL::ExecutableOp>());

  // Create our new "linked" hal.executable.
  std::string linkedExecutableName = llvm::formatv("linked_{0}", name());
  auto linkedExecutableOp = builder.create<IREE::HAL::ExecutableOp>(
      moduleOp.getLoc(), linkedExecutableName);
  SymbolTable::setSymbolVisibility(linkedExecutableOp,
                                   SymbolTable::Visibility::Private);
  // Add our hal.executable.target with an empty module.
  builder.setInsertionPointToStart(linkedExecutableOp.getBody());
  auto linkedTargetOp = builder.create<IREE::HAL::ExecutableTargetOp>(
      moduleOp.getLoc(), name(), filter_pattern());
  builder.setInsertionPoint(&linkedTargetOp.getBlock().back());
  auto linkedModuleOp = builder.create<ModuleOp>(moduleOp.getLoc());

  llvm::SmallVector<IREE::HAL::InterfaceOp, 4> interfaceOps;
  int nextEntryPointOrdinal = 0;
  DenseMap<StringRef, Operation *> symbolMap;
  DenseMap<Attribute, Attribute> entryPointRefReplacements;
  auto linkedExecutableBuilder =
      OpBuilder::atBlockBegin(linkedExecutableOp.getBody());
  auto linkedTargetBuilder = OpBuilder::atBlockBegin(linkedTargetOp.getBody());
  for (auto executableOp : executableOps) {
    auto targetOps = llvm::to_vector<4>(
        executableOp.getOps<IREE::HAL::ExecutableTargetOp>());
    for (auto targetOp : targetOps) {
      // Only process targets matching our pattern.
      if (!matchPattern(targetOp.target_backend_filter(), filter_pattern())) {
        continue;
      }

      IREE::HAL::InterfaceOp interfaceOpForExecutable;
      for (auto interfaceOp : interfaceOps) {
        if (interfaceOp.isEquivalentTo(executableOp.getFirstInterfaceOp())) {
          interfaceOpForExecutable = interfaceOp;
          break;
        }
      }
      if (!interfaceOpForExecutable) {
        interfaceOpForExecutable = dyn_cast<IREE::HAL::InterfaceOp>(
            linkedExecutableBuilder.clone(*executableOp.getFirstInterfaceOp()));
        interfaceOpForExecutable.setName(
            llvm::formatv("legacy_io_{0}", interfaceOps.size()).str());
        interfaceOps.push_back(interfaceOpForExecutable);
      }

      // Clone entry point ops and queue remapping ordinals and updating
      // symbol refs.
      for (auto entryPointOp :
           targetOp.getOps<IREE::HAL::ExecutableEntryPointOp>()) {
        auto newEntryPointOp =
            linkedTargetBuilder.create<IREE::HAL::ExecutableEntryPointOp>(
                entryPointOp.getLoc(), entryPointOp.sym_nameAttr(),
                builder.getI32IntegerAttr(nextEntryPointOrdinal++),
                builder.getSymbolRefAttr(interfaceOpForExecutable.getName()),
                entryPointOp.signatureAttr());

        // Add to replacement table for fixing up dispatch calls referencing
        // this entry point.
        auto oldSymbolRefAttr = builder.getSymbolRefAttr(
            executableOp.getName(), {builder.getSymbolRefAttr(targetOp),
                                     builder.getSymbolRefAttr(entryPointOp)});
        auto newSymbolRefAttr = builder.getSymbolRefAttr(
            linkedExecutableOp.getName(),
            {builder.getSymbolRefAttr(linkedTargetOp),
             builder.getSymbolRefAttr(newEntryPointOp)});
        entryPointRefReplacements[oldSymbolRefAttr] = newSymbolRefAttr;
      }

      mergeModuleInto(targetOp.getInnerModule(), linkedModuleOp, symbolMap);

      targetOp.erase();
    }

    if (executableOp.getOps<IREE::HAL::ExecutableTargetOp>().empty()) {
      executableOp.erase();
    }
  }

  // Update references to @executable::@target::@entry symbols.
  replaceEntryPointUses(moduleOp, entryPointRefReplacements);

  // Remove if we didn't add anything.
  if (linkedTargetOp.getOps<IREE::HAL::ExecutableEntryPointOp>().empty()) {
    linkedTargetOp.erase();
    linkedExecutableOp.erase();
  }

  return success();
}

std::array<Value, 3> LLVMBaseTargetBackend::calculateDispatchWorkgroupCount(
    Location loc, IREE::HAL::ExecutableOp executableOp,
    IREE::HAL::ExecutableEntryPointOp entryPointOp, Value workload,
    OpBuilder &builder) {
  // For now we are not tiling and just dispatch everything as 1,1,1.
  auto constantOne = builder.createOrFold<mlir::ConstantIndexOp>(loc, 1);
  return {constantOne, constantOne, constantOne};
}

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
