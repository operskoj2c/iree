// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <utility>

#include "iree/compiler/Dialect/Stream/IR/StreamDialect.h"
#include "iree/compiler/Dialect/Stream/IR/StreamOps.h"
#include "iree/compiler/Dialect/Stream/Transforms/PassDetail.h"
#include "iree/compiler/Dialect/Stream/Transforms/Passes.h"
#include "iree/compiler/Dialect/Util/IR/UtilDialect.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Stream {

// Returns true if |value| is worth outlining (large, etc).
static bool isOutlinableValue(Attribute value) {
  if (auto elementsAttr = value.dyn_cast<DenseElementsAttr>()) {
    // Don't outline splats - we want those fused.
    return !elementsAttr.isSplat();
  }
  return false;
}

struct ConstantDef {
  Operation *op;
  Type type;
  ElementsAttr value;
};

// Returns a list of all constant-like shaped data ops in the module.
static SmallVector<ConstantDef> findConstantsInModule(mlir::ModuleOp moduleOp) {
  SmallVector<ConstantDef> results;
  for (auto callableOp : moduleOp.getOps<CallableOpInterface>()) {
    for (auto &block : *callableOp.getCallableRegion()) {
      for (auto &op : block.getOperations()) {
        if (auto constantOp = dyn_cast<arith::ConstantOp>(op)) {
          if (isOutlinableValue(constantOp.getValue())) {
            results.push_back(ConstantDef{
                constantOp,
                constantOp.getType(),
                constantOp.getValue().cast<ElementsAttr>(),
            });
          }
        }
      }
    }
  }
  return results;
}

class OutlineConstantsPass : public OutlineConstantsBase<OutlineConstantsPass> {
 public:
  OutlineConstantsPass() = default;

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<mlir::StandardOpsDialect>();
    registry.insert<mlir::arith::ArithmeticDialect>();
    registry.insert<IREE::Util::UtilDialect>();
  }

  void runOnOperation() override {
    auto moduleOp = getOperation();
    if (moduleOp.getBody()->empty()) return;

    SymbolTable moduleSymbols(moduleOp);
    std::string baseName = "_constant";

    // Create all top-level util.globals from constants in the module.
    OpBuilder moduleBuilder(&moduleOp.getBody()->front());
    std::vector<std::pair<Operation *, IREE::Util::GlobalOp>> replacements;
    for (auto &def : findConstantsInModule(moduleOp)) {
      // New immutable global takes the constant attribute in its specified
      // encoding.
      auto globalOp = moduleBuilder.create<IREE::Util::GlobalOp>(
          def.op->getLoc(), baseName, /*isMutable=*/false, def.type, def.value);
      globalOp.setPrivate();
      moduleSymbols.insert(globalOp);  // uniques name
      replacements.emplace_back(def.op, globalOp);

      // Prevent the variable from being re-inlined if the canonicalizer runs.
      // By the time we've outlined things here we are sure we want them
      // outlined even if the user runs an arbitrary number of passes between
      // now and when we may use that information (HAL constant pooling, etc).
      globalOp->setAttr("noinline", moduleBuilder.getUnitAttr());
    }

    // Replace all of the constants with lookups for the new variables.
    for (auto pair : replacements) {
      auto *originalOp = pair.first;
      auto globalOp = pair.second;
      OpBuilder builder(moduleOp.getContext());
      builder.setInsertionPoint(originalOp);
      auto loadOp = builder.create<IREE::Util::GlobalLoadOp>(
          originalOp->getLoc(), globalOp.type(), SymbolRefAttr::get(globalOp));

      Value replacement;
      if (auto constantOp = dyn_cast<arith::ConstantOp>(originalOp)) {
        // Directly replace constant with global constant value.
        replacement = loadOp.result();
      } else {
        llvm_unreachable("unhandled constant op type");
      }

      originalOp->getResult(0).replaceAllUsesWith(replacement);
      originalOp->erase();
    }
  }
};

std::unique_ptr<OperationPass<mlir::ModuleOp>> createOutlineConstantsPass() {
  return std::make_unique<OutlineConstantsPass>();
}

}  // namespace Stream
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
