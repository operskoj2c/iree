// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <algorithm>
#include <iterator>

#include "iree/compiler/Dialect/Util/IR/UtilDialect.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "iree/compiler/Dialect/Util/IR/UtilTraits.h"
#include "iree/compiler/Dialect/Util/Transforms/Passes.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

#define DEBUG_TYPE "iree-util-simplify-global-accesses"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Util {

// Builds symbol ref set for all immutable globals in |moduleOp|.
static DenseSet<StringRef> gatherImmutableGlobals(mlir::ModuleOp moduleOp) {
  DenseSet<StringRef> set;
  for (auto globalOp : moduleOp.getOps<IREE::Util::GlobalOp>()) {
    if (!globalOp.is_mutable()) {
      set.insert(globalOp.sym_name());
    }
  }
  return set;
}

// Hoists all loads of immutable globals in |funcOp| to the entry block.
// |immutableGlobals| is used for lookups of which globals are immutable.
static void hoistImmutableLoads(mlir::FuncOp funcOp,
                                DenseSet<StringRef> &immutableGlobals) {
  // Since CSE of loads isn't a thing yet we perform a basic deduping here by
  // folding all subsequent loads into the first one found. This works only for
  // immutable globals as otherwise we'd have to ensure stores and
  // side-effects were properly observed.
  DenseMap<Attribute, Operation *> loadOps;
  auto *entryBlock = &funcOp.getBlocks().front();
  Operation *lastEntryOp = nullptr;
  SmallVector<std::pair<Operation *, Operation *>> opReplacements;
  for (auto &block : funcOp) {
    auto ops = llvm::to_vector<8>(block.getOps<IREE::Util::GlobalLoadOp>());
    for (auto &op : ops) {
      if (!immutableGlobals.contains(op.global())) continue;
      auto globalRef = op.globalAttr().cast<Attribute>();
      auto it = loadOps.find(globalRef);
      if (it == loadOps.end()) {
        // Move to entry block; even if it's already there (so loads are
        // hoisted at the same time).
        LLVM_DEBUG(llvm::dbgs() << "moving immutable global " << op.global()
                                << " load to the entry block\n");
        if (lastEntryOp) {
          op->moveAfter(lastEntryOp);
        } else {
          op->moveBefore(entryBlock, entryBlock->begin());
        }
        loadOps[globalRef] = op;
        lastEntryOp = op;
      } else {
        LLVM_DEBUG(llvm::dbgs()
                   << "CSE'ing immutable global " << op.global() << "\n");
        opReplacements.push_back({op, it->getSecond()});
      }
    }
  }
  for (auto &replacement : opReplacements) {
    replacement.first->replaceAllUsesWith(replacement.second);
    replacement.first->erase();
  }
}

static bool doesOpBlockMotion(Operation *op) {
  return isa<mlir::CallOpInterface>(op) ||
         op->hasTrait<OpTrait::IREE::Util::YieldPoint>();
}

static void moveOpUpInBlock(Block &block, Operation *op) {
  while (op->getPrevNode()) {
    if (doesOpBlockMotion(op->getPrevNode())) break;
    op->moveBefore(op->getPrevNode());
  }
}

static void moveOpDownInBlock(Block &block, Operation *op) {
  while (op->getNextNode() != block.getTerminator()) {
    if (doesOpBlockMotion(op->getNextNode())) break;
    op->moveAfter(op->getNextNode());
  }
}

// Optimizes the load/store ops for each given bucket.
// Returns true if any op was removed.
static bool optimizeBuckets(
    Block &block, std::map<StringRef, SmallVector<Operation *>> &buckets) {
  bool didRemoveAny = false;
  for (auto &bucket : buckets) {
    // First perform basic load-store forwarding and such.
    auto &ops = bucket.second;
    for (int i = ops.size() - 1; i >= 1; --i) {
      auto previous = ops[i - 1];
      auto current = ops[i];
      if (isa<IREE::Util::GlobalStoreOp>(previous) &&
          isa<IREE::Util::GlobalLoadOp>(current)) {
        // RAW - forward the stored global to the following use.
        auto storedValue = previous->getOperand(0);
        LLVM_DEBUG({
          llvm::dbgs() << "RAW: replacing load with previous store value:\n";
          current->dump();
          llvm::dbgs() << "->\n";
          storedValue.dump();
        });
        current->replaceAllUsesWith(ValueRange{storedValue});
        ops.erase(ops.begin() + i);
        current->erase();
        didRemoveAny = true;
      } else if (isa<IREE::Util::GlobalLoadOp>(previous) &&
                 isa<IREE::Util::GlobalLoadOp>(current)) {
        // RAR - forward the loaded global to the following use.
        LLVM_DEBUG({
          llvm::dbgs() << "RAR: replacing subsequent load with op:\n";
          current->dump();
          llvm::dbgs() << "->\n";
          previous->dump();
        });
        current->replaceAllUsesWith(previous);
        ops.erase(ops.begin() + i);
        current->erase();
        didRemoveAny = true;
      } else if (isa<IREE::Util::GlobalStoreOp>(previous) &&
                 isa<IREE::Util::GlobalStoreOp>(current)) {
        // WAW - remove the first store.
        LLVM_DEBUG({
          llvm::dbgs() << "WAW: erasing source op:\n";
          previous->dump();
          llvm::dbgs() << "\nand keeping subsequent op:\n";
          current->dump();
        });
        ops.erase(ops.begin() + i - 1);
        previous->erase();
        didRemoveAny = true;
      }
    }
    if (ops.empty()) continue;

    if (auto loadOp = dyn_cast<IREE::Util::GlobalLoadOp>(ops.front())) {
      // If the head op is a load we can move that to the top of the block.
      LLVM_DEBUG(llvm::dbgs() << "moving mutable global " << loadOp.global()
                              << " load upward\n");
      moveOpUpInBlock(block, ops.front());
    }
    if (auto storeOp = dyn_cast<IREE::Util::GlobalStoreOp>(ops.back())) {
      // If the tail op is a store we can move that to the bottom of the block.
      LLVM_DEBUG(llvm::dbgs() << "moving mutable global " << storeOp.global()
                              << " store downward\n");
      moveOpDownInBlock(block, ops.back());
    }
  }
  return didRemoveAny;
}

// Hoists loads and sinks stores to the boundary of |block| when safe.
// |immutableGlobals| is used for lookups of which globals are immutable.
//
// Basic algorithm (repeat until no op removals):
//   for each op:
//     if immutable: skip
//     add to load/store buckets (sorted vector)
//   for each bucket (symbol):
//     walk ops in reverse:
//       if (prev == store && this == load)  // RAW
//         replace load with store source
//       if (prev == load && this == load)  // RAR
//         replace with first load
//       if (prev == store && this == store) // WAW
//         remove first store
//     if (head == load) move load to front
//     if (tail == store) move store to back
//
// Returns true if there were any removals and the block should be reprocessed.
static bool rearrangeBlockGlobalAccesses(
    Block &block, DenseSet<StringRef> &immutableGlobals) {
  // Gather sequences of operations that are safe to reorder.
  // Certain ops - like calls/do_not_optimize/etc - prevent us from moving any
  // global operations across them.
  //
  // From each sequence we produce [symbol_name, [op, op, op, ...]] buckets.
  // NOTE: we use a map here so that we are deterministically ordered. This may
  // not be needed but the global count is low and it's nice to not care about
  // op order issues.
  SmallVector<std::map<StringRef, SmallVector<Operation *>>> sequencedBuckets;
  sequencedBuckets.push_back({});  // Start in a sequence.
  block.walk([&](Operation *op) {
    auto &buckets = sequencedBuckets.back();
    if (auto loadOp = dyn_cast<IREE::Util::GlobalLoadOp>(op)) {
      if (!immutableGlobals.contains(loadOp.global())) {
        buckets[loadOp.global()].push_back(op);
      }
    } else if (auto storeOp = dyn_cast<IREE::Util::GlobalStoreOp>(op)) {
      buckets[storeOp.global()].push_back(op);
    } else if (doesOpBlockMotion(op)) {
      // Split point - all accesses after this point must not assume anything
      // about accesses before it.
      if (!buckets.empty()) {
        sequencedBuckets.push_back({});
      }
    }
  });
  bool didRemoveAny = false;
  for (auto &buckets : sequencedBuckets) {
    didRemoveAny = optimizeBuckets(block, buckets) || didRemoveAny;
  }
  return didRemoveAny;
}

namespace {

class SimplifyGlobalAccessesPass
    : public PassWrapper<SimplifyGlobalAccessesPass,
                         OperationPass<mlir::FuncOp>> {
 public:
  StringRef getArgument() const override {
    return "iree-util-simplify-global-accesses";
  }

  StringRef getDescription() const override {
    return "Hoists loads and sinks stores to variables to decrease data "
           "dependency regions.";
  }

  void runOnOperation() override {
    auto funcOp = getOperation();
    if (funcOp.empty()) return;

    auto moduleOp = funcOp->getParentOfType<mlir::ModuleOp>();
    assert(moduleOp && "func not in a module");

    // Build a set of all immutable globals for fast lookup.
    auto immutableGlobals = gatherImmutableGlobals(moduleOp);

    // Hoist immutable globals first. These have no hazards and don't care
    // about control flow - like `constant` - so getting them handled first
    // avoids the need for us to do the full analysis.
    hoistImmutableLoads(funcOp, immutableGlobals);

    // We can't optimize the function if there are indirect loads/stores.
    // Note that constant loads are still ok above.
    for (auto &block : funcOp) {
      for (auto &op : block) {
        if (isa<IREE::Util::GlobalLoadIndirectOp>(op) ||
            isa<IREE::Util::GlobalStoreIndirectOp>(op)) {
          LLVM_DEBUG(llvm::dbgs()
                     << "bailing on global access simplification: indirect "
                        "accesses present in function\n");
          return;
        }
      }
    }

    // For each block in the function hoist loads and sink stores.
    // This does no cross-block movement, though it really should. Maybe when a
    // real compiler engineer sees this they'll be inspired to do this properly.
    for (auto &block : funcOp) {
      LLVM_DEBUG(llvm::dbgs() << "==== REARRANGING BLOCK ACCESSES ====\n");
      while (rearrangeBlockGlobalAccesses(block, immutableGlobals)) {
        // NOTE: block is processed until no more ops are removed. Will always
        // end in a fixed amount of time as ops are only removed from the block.
      }
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<mlir::FuncOp>>
createSimplifyGlobalAccessesPass() {
  return std::make_unique<SimplifyGlobalAccessesPass>();
}

static PassRegistration<SimplifyGlobalAccessesPass> pass;

}  // namespace Util
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
