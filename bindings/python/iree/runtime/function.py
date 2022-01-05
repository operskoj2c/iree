# Lint as: python3
# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from typing import Dict, Optional

import json
import logging

import numpy as np

from .binding import HalDevice, HalElementType, VmContext, VmFunction, VmVariantList
from . import tracing

__all__ = [
    "FunctionInvoker",
]


class Invocation:
  __slots__ = [
      "current_arg",
      "current_desc",
      "current_return_list",
      "current_return_index",
      "device",
  ]

  def __init__(self, device: HalDevice):
    self.device = device
    # Captured during arg/ret processing to emit better error messages.
    self.current_arg = None
    self.current_desc = None
    self.current_return_list = None
    self.current_return_index = 0

  def summarize_arg_error(self) -> str:
    if self.current_arg is None:
      return ""
    if isinstance(self.current_arg, np.ndarray):
      current_arg_repr = (
          f"ndarray({self.current_arg.shape}, {self.current_arg.dtype})")
    else:
      current_arg_repr = repr(self.current_arg)
    return f"{repr(current_arg_repr)} with description {self.current_desc}"

  def summarize_return_error(self) -> str:
    if self.current_return_list is None:
      return ""
    try:
      vm_repr = f"{self.current_return_index}@{self.current_return_list}"
    except:
      vm_repr = "<error printing list item>"
    return f"{vm_repr} with description {self.current_desc}"


class FunctionInvoker:
  """Wraps a VmFunction, enabling invocations against it."""
  __slots__ = [
      "_vm_context",
      "_device",
      "_vm_function",
      "_abi_dict",
      "_arg_descs",
      "_ret_descs",
      "_named_arg_indices",
      "_max_named_arg_index",
      "_has_inlined_results",
      "_tracer",
  ]

  def __init__(self, vm_context: VmContext, device: HalDevice,
               vm_function: VmFunction,
               tracer: Optional[tracing.ContextTracer]):
    self._vm_context = vm_context
    # TODO: Needing to know the precise device to allocate on here is bad
    # layering and will need to be fixed in some fashion if/when doing
    # heterogenous dispatch.
    self._device = device
    self._vm_function = vm_function
    self._tracer = tracer
    self._abi_dict = None
    self._arg_descs = None
    self._ret_descs = None
    self._has_inlined_results = False
    self._named_arg_indices: Dict[str, int] = {}
    self._max_named_arg_index: int = -1
    self._parse_abi_dict(vm_function)

  @property
  def vm_function(self) -> VmFunction:
    return self._vm_function

  def __call__(self, *args, **kwargs):
    call_trace = None  # type: Optional[tracing.CallTrace]
    if self._tracer:
      call_trace = self._tracer.start_call(self._vm_function)
    try:
      # Initialize the capacity to our total number of args, since we should
      # be below that when doing a flat invocation. May want to be more
      # conservative here when considering nesting.
      inv = Invocation(self._device)
      ret_descs = self._ret_descs

      # Merge keyword args in by name->position mapping.
      if kwargs:
        args = list(args)
        len_delta = self._max_named_arg_index - len(args) + 1
        if len_delta > 0:
          # Fill in MissingArgument placeholders before arranging kwarg input.
          # Any remaining placeholders will fail arity checks later on.
          args.extend([MissingArgument] * len_delta)

        for kwarg_key, kwarg_value in kwargs.items():
          try:
            kwarg_index = self._named_arg_indices[kwarg_key]
          except KeyError:
            raise ArgumentError(f"specified kwarg '{kwarg_key}' is unknown")
          args[kwarg_index] = kwarg_value

      arg_list = VmVariantList(len(args))
      ret_list = VmVariantList(len(ret_descs) if ret_descs is not None else 1)
      _merge_python_sequence_to_vm(inv, arg_list, args, self._arg_descs)
      if call_trace:
        call_trace.add_vm_list(arg_list, "args")
      self._vm_context.invoke(self._vm_function, arg_list, ret_list)
      if call_trace:
        call_trace.add_vm_list(ret_list, "results")

      # Un-inline the results to align with reflection, as needed.
      reflection_aligned_ret_list = ret_list
      if self._has_inlined_results:
        reflection_aligned_ret_list = VmVariantList(1)
        reflection_aligned_ret_list.push_list(ret_list)
      returns = _extract_vm_sequence_to_python(inv, reflection_aligned_ret_list,
                                               ret_descs)
      return_arity = len(returns)
      if return_arity == 1:
        return returns[0]
      elif return_arity == 0:
        return None
      else:
        return tuple(returns)
    finally:
      if call_trace:
        call_trace.end_call()

  def _parse_abi_dict(self, vm_function: VmFunction):
    reflection = vm_function.reflection
    abi_json = reflection.get("iree.abi")
    if abi_json is None:
      # It is valid to have no reflection data, and rely on pure dynamic
      # dispatch.
      logging.debug(
          "Function lacks reflection data. Interop will be limited: %r",
          vm_function)
      return
    try:
      self._abi_dict = json.loads(abi_json)
    except json.JSONDecodeError as e:
      raise RuntimeError(
          f"Reflection metadata is not valid JSON: {abi_json}") from e
    try:
      self._arg_descs = self._abi_dict["a"]
      self._ret_descs = self._abi_dict["r"]
    except KeyError as e:
      raise RuntimeError(
          f"Malformed function reflection metadata: {reflection}") from e
    if not isinstance(self._arg_descs, list) or not isinstance(
        self._ret_descs, list):
      raise RuntimeError(
          f"Malformed function reflection metadata structure: {reflection}")

    # Post-process the arg descs to transform "named" records to just their
    # type, stashing the index.
    for i in range(len(self._arg_descs)):
      maybe_named_desc = self._arg_descs[i]
      if maybe_named_desc and maybe_named_desc[0] == "named":
        arg_name, arg_type_desc = maybe_named_desc[1:]
        self._arg_descs[i] = arg_type_desc
        self._named_arg_indices[arg_name] = i
        if i > self._max_named_arg_index:
          self._max_named_arg_index = i

    # Detect whether the results are a slist/stuple/sdict, which indicates
    # that they are inlined with the function's results.
    if len(self._ret_descs) == 1:
      maybe_inlined = self._ret_descs[0]
      if maybe_inlined and maybe_inlined[0] in ["slist", "stuple", "sdict"]:
        self._has_inlined_results = True

  def __repr__(self):
    return repr(self._vm_function)


# Python type to VM Type converters. All of these take:
#   inv: Invocation
#   target_list: VmVariantList to append to
#   python_value: The python value of the given type
#   desc: The ABI descriptor list (or None if in dynamic mode).


def _bool_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  _int_to_vm(inv, t, int(x), desc)


def _int_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  # Implicit conversion to a 0d tensor.
  if desc and _is_0d_ndarray_descriptor(desc):
    casted = _cast_scalar_to_ndarray(inv, x, desc)
    _ndarray_to_vm(inv, t, casted, desc)
    return
  t.push_int(x)


def _float_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  # Implicit conversion to a 0d tensor.
  if desc and _is_0d_ndarray_descriptor(desc):
    casted = _cast_scalar_to_ndarray(inv, x, desc)
    _ndarray_to_vm(inv, t, casted, desc)
    return
  t.push_float(x)


def _list_or_tuple_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  desc_type = desc[0]
  if desc_type != "slist" and desc_type != "stuple":
    _raise_argument_error(inv,
                          f"passed a list or tuple but expected {desc_type}")
  # When decoding a list or tuple, the desc object is like:
  # ['slist', [...value_type_0...], ...]
  # Where the type is either 'slist' or 'stuple'.
  sub_descriptors = desc[1:]
  arity = len(sub_descriptors)
  if len(x) != arity:
    _raise_argument_error(inv,
                          f"mismatched list/tuple arity: {len(x)} vs {arity}")
  sub_list = VmVariantList(arity)
  _merge_python_sequence_to_vm(inv, sub_list, x, sub_descriptors)
  t.push_list(sub_list)


def _dict_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  desc_type = desc[0]
  if desc_type != "sdict":
    _raise_argument_error(inv, f"passed a dict but expected {desc_type}")
  # When decoding a dict, the desc object is like:
  # ['sdict', ['key0', [...value_type_0...]], ['key1', [...value_type_1...]]]]
  sub_descriptors = desc[1:]
  py_values = []
  value_descs = []
  for key, value_desc in sub_descriptors:
    try:
      py_values.append(x[key])
    except KeyError:
      _raise_argument_error(inv, f"expected dict item with key '{key}'")
    value_descs.append(value_desc)

  sub_list = VmVariantList(len(py_values))
  _merge_python_sequence_to_vm(inv, sub_list, py_values, value_descs)
  t.push_list(sub_list)


def _str_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  _raise_argument_error(inv, "Python str arguments not yet supported")


def _ndarray_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  # Validate and implicit conversion against type descriptor.
  if desc is not None:
    desc_type = desc[0]
    if desc_type != "ndarray":
      _raise_argument_error(inv, f"passed an ndarray but expected {desc_type}")
    dtype_str = desc[1]
    try:
      dtype = ABI_TYPE_TO_DTYPE[dtype_str]
    except KeyError:
      _raise_argument_error(inv, f"unrecognized dtype '{dtype_str}'")
    if dtype != x.dtype:
      x = x.astype(dtype)
    rank = desc[2]
    shape = desc[3:]
    ndarray_shape = x.shape
    if len(shape) != len(ndarray_shape) or rank != len(ndarray_shape):
      _raise_argument_error(
          inv, f"rank mismatch {len(ndarray_shape)} vs {len(shape)}")
    for exp_dim, act_dim in zip(shape, ndarray_shape):
      if exp_dim is not None and exp_dim != act_dim:
        _raise_argument_error(
            inv, f"shape mismatch {ndarray_shape} vs {tuple(shape)}")
  actual_dtype = x.dtype
  for match_dtype, element_type in DTYPE_TO_HAL_ELEMENT_TYPE:
    if match_dtype == actual_dtype:
      break
  else:
    _raise_argument_error(inv, f"unsupported numpy dtype {x.dtype}")
  t.push_buffer_view(inv.device, x, element_type)


def _ndarray_like_to_vm(inv: Invocation, t: VmVariantList, x, desc):
  return _ndarray_to_vm(inv, t, np.asarray(x), desc)


class _MissingArgument:
  """Placeholder for missing kwargs in the function input."""

  def __repr__(self):
    return "<mising argument>"


MissingArgument = _MissingArgument()

PYTHON_TO_VM_CONVERTERS = {
    bool: _bool_to_vm,
    int: _int_to_vm,
    float: _float_to_vm,
    list: _list_or_tuple_to_vm,
    tuple: _list_or_tuple_to_vm,
    dict: _dict_to_vm,
    str: _str_to_vm,
    np.ndarray: _ndarray_to_vm,
}

# VM to Python converters. All take:
#   inv: Invocation
#   vm_list: VmVariantList to read from
#   vm_index: Index in the vm_list to extract
#   desc: The ABI descriptor list (or None if in dynamic mode)
# Return the corresponding Python object.


def _vm_to_ndarray(inv: Invocation, vm_list: VmVariantList, vm_index: int,
                   desc):
  # The descriptor for an ndarray is like:
  #   ["ndarray", "<dtype>", <rank>, <dim>...]
  #   ex: ['ndarray', 'i32', 1, 25948]
  x = vm_list.get_as_ndarray(vm_index)
  dtype_str = desc[1]
  try:
    dtype = ABI_TYPE_TO_DTYPE[dtype_str]
  except KeyError:
    _raise_return_error(inv, f"unrecognized dtype '{dtype_str}'")
  if dtype != x.dtype:
    x = x.astype(dtype)
  return x


def _vm_to_sdict(inv: Invocation, vm_list: VmVariantList, vm_index: int, desc):
  # The descriptor for an sdict is like:
  #   ['sdict', ['key1', value1], ...]
  sub_vm_list = vm_list.get_as_list(vm_index)
  item_keys = []
  item_descs = []
  for k, d in desc[1:]:
    item_keys.append(k)
    item_descs.append(d)
  py_items = _extract_vm_sequence_to_python(inv, sub_vm_list, item_descs)
  return dict(zip(item_keys, py_items))


def _vm_to_slist(inv: Invocation, vm_list: VmVariantList, vm_index: int, desc):
  # The descriptor for an slist is like:
  #   ['slist, item1, ...]
  sub_vm_list = vm_list.get_as_list(vm_index)
  item_descs = desc[1:]
  py_items = _extract_vm_sequence_to_python(inv, sub_vm_list, item_descs)
  return py_items


def _vm_to_stuple(inv: Invocation, vm_list: VmVariantList, vm_index: int, desc):
  return tuple(_vm_to_slist(inv, vm_list, vm_index, desc))


def _vm_to_scalar(type_bound: type):

  def convert(inv: Invocation, vm_list: VmVariantList, vm_index: int, desc):
    value = vm_list.get_variant(vm_index)
    if not isinstance(value, type_bound):
      raise ReturnError(
          f"expected an {type_bound} value but got {value.__class__}")
    return value

  return convert


def _vm_to_pylist(inv: Invocation, vm_list: VmVariantList, vm_index: int, desc):
  # The descriptor for a pylist is like:
  #   ['pylist', element_type]
  sub_vm_list = vm_list.get_as_list(vm_index)
  element_type_desc = desc[1:]
  py_items = _extract_vm_sequence_to_python(
      inv, sub_vm_list, element_type_desc * len(sub_vm_list))
  return py_items


VM_TO_PYTHON_CONVERTERS = {
    "ndarray": _vm_to_ndarray,
    "sdict": _vm_to_sdict,
    "slist": _vm_to_slist,
    "stuple": _vm_to_stuple,
    "py_homogeneous_list": _vm_to_pylist,

    # Scalars.
    "i8": _vm_to_scalar(int),
    "i16": _vm_to_scalar(int),
    "i32": _vm_to_scalar(int),
    "i64": _vm_to_scalar(int),
    "f16": _vm_to_scalar(float),
    "f32": _vm_to_scalar(float),
    "f64": _vm_to_scalar(float),
    "bf16": _vm_to_scalar(float),
}

ABI_TYPE_TO_DTYPE = {
    # TODO: Others.
    "f32": np.float32,
    "i32": np.int32,
    "i64": np.int64,
    "f64": np.float64,
    "i16": np.int16,
    "i8": np.int8,
    "i1": np.bool_,
}

# NOTE: Numpy dtypes are not hashable and exist in a hierarchy that should
# be queried via isinstance checks. This should be done as a fallback but
# this is a linear list for quick access to the most common. There may also
# be a better way to do this.
DTYPE_TO_HAL_ELEMENT_TYPE = (
    (np.float32, HalElementType.FLOAT_32),
    (np.float64, HalElementType.FLOAT_64),
    (np.float16, HalElementType.FLOAT_16),
    (np.int32, HalElementType.SINT_32),
    (np.int64, HalElementType.SINT_64),
    (np.int16, HalElementType.SINT_16),
    (np.int8, HalElementType.SINT_8),
    (np.uint32, HalElementType.UINT_32),
    (np.uint64, HalElementType.UINT_64),
    (np.uint16, HalElementType.UINT_16),
    (np.uint8, HalElementType.UINT_8),
    (np.bool_, HalElementType.BOOL_8),
)


def _is_ndarray_descriptor(desc):
  return desc and desc[0] == "ndarray"


def _is_0d_ndarray_descriptor(desc):
  # Example: ["ndarray", "f32", 0]
  return desc and desc[0] == "ndarray" and desc[2] == 0


def _cast_scalar_to_ndarray(inv: Invocation, x, desc):
  # Example descriptor: ["ndarray", "f32", 0]
  dtype_str = desc[1]
  try:
    dtype = ABI_TYPE_TO_DTYPE[dtype_str]
  except KeyError:
    _raise_argument_error(inv, f"unrecognized dtype '{dtype_str}'")
  return dtype(x)


class ArgumentError(ValueError):
  pass


class ReturnError(ValueError):
  pass


def _raise_argument_error(inv: Invocation,
                          summary: str,
                          e: Optional[Exception] = None):
  new_e = ArgumentError(
      f"Error passing argument: {summary} "
      f"(while encoding argument {inv.summarize_arg_error()})")
  if e:
    raise new_e from e
  else:
    raise new_e


def _raise_return_error(inv: Invocation,
                        summary: str,
                        e: Optional[Exception] = None):
  new_e = ReturnError(f"Error processing function return: {summary} "
                      f"(while decoding return {inv.summarize_return_error()})")
  if e:
    raise new_e from e
  else:
    raise new_e


def _merge_python_sequence_to_vm(inv: Invocation, vm_list, py_list, descs):
  # For dynamic mode, just assume we have the right arity.
  if descs is None:
    descs = [None] * len(py_list)
  else:
    len_py_list = sum([1 for x in py_list if x is not MissingArgument])
    if len(py_list) != len_py_list:
      _raise_argument_error(
          inv,
          f"mismatched call arity: expected {len(descs)} arguments but got "
          f"{len_py_list}. Expected signature=\n{descs}\nfor input=\n{py_list}")

  for py_value, desc in zip(py_list, descs):
    inv.current_arg = py_value
    inv.current_desc = desc
    py_type = py_value.__class__

    # For ndarray, we want to be able to handle array-like, so check for that
    # explicitly (duck typed vs static typed).
    if _is_ndarray_descriptor(desc):
      converter = _ndarray_like_to_vm
    else:
      try:
        converter = PYTHON_TO_VM_CONVERTERS[py_type]
      except KeyError:
        _raise_argument_error(
            inv, f"cannot map Python type to VM: {py_type}"
            f" (for desc {desc})")
    try:
      converter(inv, vm_list, py_value, desc)
    except ArgumentError:
      raise
    except Exception as e:
      _raise_argument_error(inv, f"exception converting from Python type to VM",
                            e)


def _extract_vm_sequence_to_python(inv: Invocation, vm_list, descs):
  vm_list_arity = len(vm_list)
  if descs is None:
    descs = [None] * vm_list_arity
  elif vm_list_arity != len(descs):
    _raise_return_error(
        inv, f"mismatched return arity: {vm_list_arity} vs {len(descs)}")
  results = []
  for vm_index, desc in zip(range(vm_list_arity), descs):
    inv.current_return_list = vm_list
    inv.current_return_index = vm_index
    inv.current_desc = desc
    if desc is None:
      # Dynamic (non reflection mode).
      converted = vm_list.get_variant(vm_index)
    else:
      # Known type descriptor.
      vm_type = desc if isinstance(desc, str) else desc[0]
      try:
        converter = VM_TO_PYTHON_CONVERTERS[vm_type]
      except KeyError:
        _raise_return_error(inv, f"cannot map VM type to Python: {vm_type}")
      try:
        converted = converter(inv, vm_list, vm_index, desc)
      except ReturnError:
        raise
      except Exception as e:
        _raise_return_error(inv, f"exception converting from VM type to Python",
                            e)
    results.append(converted)
  return results
