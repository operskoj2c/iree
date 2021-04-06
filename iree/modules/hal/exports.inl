// Copyright 2021 Google LLC
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

//===----------------------------------------------------------------------===//
//
//         ██     ██  █████  ██████  ███    ██ ██ ███    ██  ██████
//         ██     ██ ██   ██ ██   ██ ████   ██ ██ ████   ██ ██
//         ██  █  ██ ███████ ██████  ██ ██  ██ ██ ██ ██  ██ ██   ███
//         ██ ███ ██ ██   ██ ██   ██ ██  ██ ██ ██ ██  ██ ██ ██    ██
//          ███ ███  ██   ██ ██   ██ ██   ████ ██ ██   ████  ██████
//
//===----------------------------------------------------------------------===//
//
// This file will be auto generated from hal.imports.mlir in the future; for
// now it's modified by hand but with strict alphabetical sorting required.
// The order of these functions must be sorted ascending by name in a way
// compatible with iree_string_view_compare.
//
// Users are meant to `#define EXPORT_FN` to be able to access the information.
// #define EXPORT_FN(name, arg_type, ret_type, target_fn)

// clang-format off

EXPORT_FN("allocator.allocate", iree_hal_module_allocator_allocate, riii, r)
EXPORT_FN("allocator.wrap.byte_buffer", iree_hal_module_allocator_wrap_byte_buffer, riirii, r)

EXPORT_FN("buffer.allocator", iree_hal_module_buffer_allocator, r, r)
EXPORT_FN("buffer.load", iree_hal_module_buffer_load, rii, i)
EXPORT_FN("buffer.store", iree_hal_module_buffer_store, irii, v)
EXPORT_FN("buffer.subspan", iree_hal_module_buffer_subspan, rii, r)

EXPORT_FN("buffer_view.buffer", iree_hal_module_buffer_view_buffer, r, r)
EXPORT_FN("buffer_view.byte_length", iree_hal_module_buffer_view_byte_length, r, i)
EXPORT_FN("buffer_view.create", iree_hal_module_buffer_view_create, riCiD, r)
EXPORT_FN("buffer_view.dim", iree_hal_module_buffer_view_dim, ri, i)
EXPORT_FN("buffer_view.element_type", iree_hal_module_buffer_view_element_type, r, i)
EXPORT_FN("buffer_view.rank", iree_hal_module_buffer_view_rank, r, i)
EXPORT_FN("buffer_view.trace", iree_hal_module_buffer_view_trace, rCrD, v)

EXPORT_FN("command_buffer.begin", iree_hal_module_command_buffer_begin, r, v)
EXPORT_FN("command_buffer.bind_descriptor_set", iree_hal_module_command_buffer_bind_descriptor_set, rrirCiD, v)
EXPORT_FN("command_buffer.copy_buffer", iree_hal_module_command_buffer_copy_buffer, rririi, v)
EXPORT_FN("command_buffer.create", iree_hal_module_command_buffer_create, rii, r)
EXPORT_FN("command_buffer.dispatch", iree_hal_module_command_buffer_dispatch, rriiii, v)
EXPORT_FN("command_buffer.dispatch.indirect", iree_hal_module_command_buffer_dispatch_indirect, rriri, v)
EXPORT_FN("command_buffer.end", iree_hal_module_command_buffer_end, r, v)
EXPORT_FN("command_buffer.execution_barrier", iree_hal_module_command_buffer_execution_barrier, riii, v)
EXPORT_FN("command_buffer.fill_buffer", iree_hal_module_command_buffer_fill_buffer, rriii, v)
EXPORT_FN("command_buffer.push_constants", iree_hal_module_command_buffer_push_constants, rriCiD, v)
EXPORT_FN("command_buffer.push_descriptor_set", iree_hal_module_command_buffer_push_descriptor_set, rriCiriiD, v)

EXPORT_FN("descriptor_set.create", iree_hal_module_descriptor_set_create, rrCiriiD, r)

EXPORT_FN("descriptor_set_layout.create", iree_hal_module_descriptor_set_layout_create, riCiiiD, r)

EXPORT_FN("device.allocator", iree_hal_module_device_allocator, r, r)
EXPORT_FN("device.match.id", iree_hal_module_device_match_id, rr, i)

EXPORT_FN("ex.shared_device", iree_hal_module_ex_shared_device, v, r)
EXPORT_FN("ex.submit_and_wait", iree_hal_module_ex_submit_and_wait, rr, v)

EXPORT_FN("executable.create", iree_hal_module_executable_create, rirCrD, r)

EXPORT_FN("executable_layout.create", iree_hal_module_executable_layout_create, riCrD, r)

EXPORT_FN("semaphore.await", iree_hal_module_semaphore_await, ri, i)
EXPORT_FN("semaphore.create", iree_hal_module_semaphore_create, ri, r)
EXPORT_FN("semaphore.fail", iree_hal_module_semaphore_fail, r, i)
EXPORT_FN("semaphore.query", iree_hal_module_semaphore_query, r, ii)
EXPORT_FN("semaphore.signal", iree_hal_module_semaphore_signal, ri, v)

// clang-format on
