// RUN: iree-opt -split-input-file -iree-vm-mark-public-symbols-exported %s | IreeFileCheck %s

// This file is used as an example in docs/developer_overview.md.
// If you move or delete it, please update the documentation accordingly.

// CHECK-LABEL: @private_symbol
// CHECK-SAME: {sym_visibility = "private"}
func @private_symbol() attributes {sym_visibility = "private"}

// CHECK-LABEL: @public_symbol
// CHECK-SAME: {iree.module.export}
func @public_symbol()
