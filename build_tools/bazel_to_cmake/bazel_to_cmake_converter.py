# Lint as: python3
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Converter class for converting Bazel BUILD files to CMakeLists.txt files.

See bazel_to_cmake.py for usage.
"""

# pylint: disable=missing-docstring
# pylint: disable=invalid-name
# pylint: disable=unused-argument
# pylint: disable=exec-used

import itertools
import textwrap

import bazel_to_cmake_targets

# ------------------------------------------------------------------------- #
# Conversion utilities, written to reduce boilerplate and allow for reuse   #
# between similar rule conversions (e.g. cc_library and cc_binary).         #
# ------------------------------------------------------------------------- #


def _expand_cmake_var(var):
  return "${" + var + "}"


def _convert_string_arg_block(name, value, quote=True):
  #  NAME
  #    "value"
  if value is None:
    return ""
  if quote:
    return f'  {name}\n    "{value}"\n'
  else:
    return f"  {name}\n    {value}\n"


def _convert_string_list_block(name, values, quote=True, sort=False):
  # Note this deliberately distinguishes between an empty list (argument
  # explicitly specified) and None (argument left as default).
  if values is None:
    return ""

  if sort:
    values = sorted(values)

  if quote:
    values_list = "\n".join([f'    "{v}"' for v in values])
  else:
    values_list = "\n".join([f"    {v}" for v in values])

  return f"  {name}\n{values_list}\n"


def _convert_option_block(option, option_value):
  if option_value:
    # Note: this is a truthiness check as well as an existence check, e.g.
    # Bazel `testonly = False` will be handled correctly by this condition.
    return f"  {option}\n"
  else:
    return ""


def _convert_translate_tool_block(translate_tool):
  if translate_tool is None:
    return ""
  # Bazel target name to cmake binary name
  # Bazel `//iree/custom:custom-translate` -> CMake `iree_custom_custom-translate`
  translate_tool = translate_tool.replace(
      "//iree", "iree")  # iree/custom:custom-translate
  translate_tool = translate_tool.replace(":",
                                          "_")  # iree/custom_custom-translate
  translate_tool = translate_tool.replace("/",
                                          "_")  # iree_custom_custom-translate
  return _convert_string_arg_block("TRANSLATE_TOOL",
                                   translate_tool,
                                   quote=False)


def _convert_srcs_block(srcs):
  if srcs is None:
    return ""
  generated_srcs = [src for src in srcs if src.startswith(":")]
  srcs = [src for src in srcs if src not in generated_srcs]
  sets = []
  if srcs:
    sets.append(_convert_string_list_block("SRCS", srcs, sort=True))
  if generated_srcs:
    sets.append(
        _convert_string_list_block("GENERATED_SRCS",
                                   [src[1:] for src in generated_srcs],
                                   sort=True))
  return "\n".join(sets)


def _convert_td_file_block(td_file):
  if td_file.startswith("//iree"):
    # Bazel `//iree/dir/td_file.td`
    # -> CMake `${IREE_ROOT_DIR}/iree/dir/td_file.td
    # Bazel `//iree/dir/IR:td_file.td`
    # -> CMake `${IREE_ROOT_DIR}/iree/dir/IR/td_file.td
    td_file = td_file.replace("//iree", "${IREE_ROOT_DIR}/iree")
    td_file = td_file.replace(":", "/")
  return _convert_string_arg_block("TD_FILE", td_file)


def _convert_tbl_outs_block(tbl_outs):
  outs_list = "\n".join([f"    {flag} {value}" for flag, value in tbl_outs])
  return f"  OUTS\n{outs_list}\n"


def _convert_tblgen_block(tblgen):
  if tblgen.endswith("iree-tblgen"):
    return "  TBLGEN\n    IREE\n"
  else:
    return ""


def _convert_target(target):
  """Returns a list of targets that correspond to the specified Bazel target.
  Note that this must be a list because some targets have a one to many mapping.
  """
  if target.startswith(":") and target.endswith(("_gen", "Gen")):
    # Files created by gentbl have to be included as source and header files
    # and not as a dependency. Adding these targets to the dependencies list,
    # results in linkage failures if the library including the gentbl dep is
    # marked as ALWAYSLINK.
    # This drops deps in the local namespace ending with '_gen' and 'Gen'
    target = [""]
  elif not target.startswith(("//bindings", "//experimental", "//iree", ":")):
    # External target, call helper method for special case handling.
    target = bazel_to_cmake_targets.convert_external_target(target)
  else:
    # Bazel `:api`            -> CMake `::api`
    # Bazel `//iree/base`     -> CMake `iree::base`
    # Bazel `//iree/base:api` -> CMake `iree::base::api`
    target = target.replace("//bindings", "bindings")  # bindings:api
    # Support for experimental targets is best effort with no guarantees.
    target = target.replace("//experimental",
                            "experimental")  # experimental:api
    target = target.replace("//iree", "iree")  # iree/base:api
    target = target.replace(":", "::")  # iree/base::api or ::api
    target = target.replace("/", "::")  # iree::base::api
    target = [target]
  return target


def _convert_single_target(target):
  replacement_targets = _convert_target(target)
  if len(replacement_targets) != 1:
    raise RuntimeError(f"Expected single target replacement for {target},"
                       f" but got multiple: {replacement_targets}")
  return replacement_targets[0]


def _convert_single_target_block(name, target):
  mapped_target = _convert_single_target(target)
  return _convert_string_arg_block(name, mapped_target, quote=False)


def _convert_target_list_block(list_name, targets):
  if targets is None:
    return ""

  #  DEPS
  #    package1::target1
  #    package1::target2
  #    package2::target
  targets = [_convert_target(t) for t in targets]
  # Flatten lists
  targets = list(itertools.chain.from_iterable(targets))
  # Remove duplicates
  targets = set(targets)
  # Remove Falsey (None and empty string) values
  targets = filter(None, targets)

  return _convert_string_list_block(list_name, targets, sort=True, quote=False)


class BuildFileFunctions(object):
  """Object passed to `exec` that has handlers for BUILD file functions."""

  def __init__(self, converter):
    self.converter = converter

  def _convert_unimplemented_function(self, function, details=""):
    message = f"Unimplemented {function}: {details}"
    if not self.converter.first_error:
      self.converter.first_error = NotImplementedError(message)
    # Avoid submitting the raw results from non-strict runs. These are still
    # useful but are generally not safe to submit as-is. An upstream check
    # prevents changes with this phrase from being submitted.
    # Written as separate literals to avoid the check triggering here.
    submit_blocker = "DO" + " NOT" + " SUBMIT."
    self.converter.body += f"# {submit_blocker} {message}\n"

  # ------------------------------------------------------------------------- #
  # Function handlers that convert BUILD definitions to CMake definitions.    #
  #                                                                           #
  # Names and signatures must match 1:1 with those expected in BUILD files    #
  # except that default values for optional arguments should generally be     #
  # `None` so we don't set them unnecessarily in the CMakeLists.txt files.    #
  # Each function that may be found in a BUILD file must be listed here.      #
  # ------------------------------------------------------------------------- #

  # Functions with no mapping to CMake. Just ignore these.
  def load(self, *args, **kwargs):
    pass

  def package(self, **kwargs):
    pass

  def iree_build_test(self, **kwargs):
    pass

  def test_suite(self, **kwargs):
    pass

  def config_setting(self, **kwargs):
    pass

  def exports_files(self, *args, **kwargs):
    pass

  def filegroup(self, name, **kwargs):
    # Not implemented yet. Might be a no-op, or may want to evaluate the srcs
    # attribute and pass them along to any targets that depend on the filegroup.
    # Cross-package dependencies and complicated globs could be hard to handle.

    # We have a bunch of filegroups that just contain TD files. CMake doesn't
    # model this at all, so we'll just hardcode this special case.
    # TODO(gcmn): Handle this robustly
    if name == "td_files":
      return

    self._convert_unimplemented_function("filegroup", name)

  def sh_binary(self, name, **kwargs):
    self._convert_unimplemented_function("sh_binary", name)

  def glob(self, include, exclude=None, exclude_directories=1):
    if exclude_directories != 1:
      self._convert_unimplemented_function("glob", "with exclude_directories")
    if exclude is None:
      exclude = []

    glob_vars = []
    for pattern in include:
      if "**" in pattern:
        # bazel's glob has some specific restrictions about crossing package
        # boundaries. We have no uses of recursive globs. Rather than try to
        # emulate them or silently give different behavior, just error out.
        # See https://docs.bazel.build/versions/master/be/functions.html#glob
        raise NotImplementedError("Recursive globs not supported")
      # Bazel `*.mlir` glob -> CMake Variable `_GLOB_X_MLIR`
      var = "_GLOB_" + pattern.replace("*", "X").replace(".", "_").upper()
      glob_vars.append(var)
      self.converter.body += (
          f"file(GLOB {var} LIST_DIRECTORIES false"
          f" RELATIVE {_expand_cmake_var('CMAKE_CURRENT_SOURCE_DIR')}"
          f" CONFIGURE_DEPENDS {pattern})\n")
    for pattern in exclude:
      if "**" in pattern:
        raise NotImplementedError("Recursive globs not supported")
      exclude_var = ("_GLOB_" +
                     pattern.replace("*", "X").replace(".", "_").upper())
      self.converter.body += (
          f"file(GLOB {exclude_var} LIST_DIRECTORIES false"
          f" RELATIVE {_expand_cmake_var('CMAKE_CURRENT_SOURCE_DIR')}"
          f" CONFIGURE_DEPENDS {pattern})\n")
      for glob_var in glob_vars:
        self.converter.body += (
            f"list(REMOVE_ITEM {glob_var} {_expand_cmake_var(exclude_var)})\n")
    return [_expand_cmake_var(var) for var in glob_vars]

  # TODO(gcmn) implement these types of functions in a less hard-coded way
  def platform_trampoline_deps(self, basename, path="base"):
    return [f"//iree/{path}/internal:{basename}_internal"]

  def select(self, d):
    self._convert_unimplemented_function("select", str(d))
    return d["//conditions:default"]

  def cc_library(self,
                 name,
                 hdrs=None,
                 textual_hdrs=None,
                 srcs=None,
                 data=None,
                 deps=None,
                 defines=None,
                 testonly=None,
                 linkopts=None,
                 **kwargs):
    if linkopts:
      self._convert_unimplemented_function("linkopts")
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    hdrs_block = _convert_string_list_block("HDRS", hdrs, sort=True)
    textual_hdrs_block = _convert_string_list_block("TEXTUAL_HDRS",
                                                    textual_hdrs,
                                                    sort=True)
    srcs_block = _convert_srcs_block(srcs)
    data_block = _convert_target_list_block("DATA", data)
    deps_block = _convert_target_list_block("DEPS", deps)
    defines_block = _convert_string_list_block("DEFINES", defines)
    testonly_block = _convert_option_block("TESTONLY", testonly)

    self.converter.body += (f"iree_cc_library(\n"
                            f"{name_block}"
                            f"{hdrs_block}"
                            f"{textual_hdrs_block}"
                            f"{srcs_block}"
                            f"{data_block}"
                            f"{deps_block}"
                            f"{defines_block}"
                            f"{testonly_block}"
                            f"  PUBLIC\n)\n\n")

  def cc_test(self,
              name,
              hdrs=None,
              srcs=None,
              data=None,
              deps=None,
              tags=None,
              **kwargs):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    hdrs_block = _convert_string_list_block("HDRS", hdrs, sort=True)
    srcs_block = _convert_srcs_block(srcs)
    data_block = _convert_target_list_block("DATA", data)
    deps_block = _convert_target_list_block("DEPS", deps)
    labels_block = _convert_string_list_block("LABELS", tags)

    self.converter.body += (f"iree_cc_test(\n"
                            f"{name_block}"
                            f"{hdrs_block}"
                            f"{srcs_block}"
                            f"{data_block}"
                            f"{deps_block}"
                            f"{labels_block}"
                            f")\n\n")

  def cc_binary(self,
                name,
                srcs=None,
                data=None,
                deps=None,
                linkopts=None,
                testonly=None,
                **kwargs):
    if linkopts:
      self._convert_unimplemented_function("linkopts")
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    data_block = _convert_target_list_block("DATA", data)
    deps_block = _convert_target_list_block("DEPS", deps)
    testonly_block = _convert_option_block("TESTONLY", testonly)

    self.converter.body += (f"iree_cc_binary(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{data_block}"
                            f"{deps_block}"
                            f"{testonly_block}"
                            f")\n\n")

  # Effectively an alias in IREE code.
  iree_cc_binary = cc_binary

  def cc_embed_data(self,
                    name,
                    srcs,
                    cc_file_output,
                    h_file_output,
                    testonly=None,
                    cpp_namespace=None,
                    strip_prefix=None,
                    flatten=None,
                    identifier=None,
                    **kwargs):
    if identifier:
      self._convert_unimplemented_function("cc_embed_data",
                                           name + " has identifier")
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    cc_file_output_block = _convert_string_arg_block("CC_FILE_OUTPUT",
                                                     cc_file_output)
    h_file_output_block = _convert_string_arg_block("H_FILE_OUTPUT",
                                                    h_file_output)
    testonly_block = _convert_option_block("TESTONLY", testonly)
    namespace_block = _convert_string_arg_block("CPP_NAMESPACE", cpp_namespace)
    flatten_block = _convert_option_block("FLATTEN", flatten)

    self.converter.body += (f"iree_cc_embed_data(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{cc_file_output_block}"
                            f"{h_file_output_block}"
                            f"{testonly_block}"
                            f"{namespace_block}"
                            f"{flatten_block}"
                            f"  PUBLIC\n)\n\n")

  def spirv_kernel_cc_library(self, name, srcs):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)

    self.converter.body += (f"iree_spirv_kernel_cc_library(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f")\n\n")

  def iree_bytecode_module(self,
                           name,
                           src,
                           flags=None,
                           translate_tool=None,
                           cc_namespace=None,
                           testonly=None):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    src_block = _convert_string_arg_block("SRC", src)
    namespace_block = _convert_string_arg_block("CC_NAMESPACE", cc_namespace)
    translate_tool_block = _convert_translate_tool_block(translate_tool)
    flags_block = _convert_string_list_block("FLAGS", flags)
    testonly_block = _convert_option_block("TESTONLY", testonly)

    self.converter.body += (f"iree_bytecode_module(\n"
                            f"{name_block}"
                            f"{src_block}"
                            f"{namespace_block}"
                            f"{translate_tool_block}"
                            f"{flags_block}"
                            f"{testonly_block}"
                            f"  PUBLIC\n)\n\n")

  def iree_flatbuffer_c_library(self, name, srcs, flatcc_args=None):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    flatcc_args_block = _convert_string_list_block("FLATCC_ARGS", flatcc_args)

    self.converter.body += (f"flatbuffer_c_library(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{flatcc_args_block}"
                            f"  PUBLIC\n)\n\n")

  def gentbl(self,
             name,
             tblgen,
             td_file,
             tbl_outs,
             td_srcs=None,
             td_includes=None,
             strip_include_prefix=None,
             test=None):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    tblgen_block = _convert_tblgen_block(tblgen)
    td_file_block = _convert_td_file_block(td_file)
    outs_block = _convert_tbl_outs_block(tbl_outs)

    self.converter.body += (f"iree_tablegen_library(\n"
                            f"{name_block}"
                            f"{td_file_block}"
                            f"{outs_block}"
                            f"{tblgen_block}"
                            f")\n\n")

  def iree_tablegen_doc(self,
                        name,
                        tblgen,
                        td_file,
                        tbl_outs,
                        td_srcs=None,
                        td_includes=None,
                        strip_include_prefix=None):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    tblgen_block = _convert_tblgen_block(tblgen)
    td_file_block = _convert_td_file_block(td_file)
    outs_block = _convert_tbl_outs_block(tbl_outs)

    self.converter.body += (f"iree_tablegen_doc(\n"
                            f"{name_block}"
                            f"{td_file_block}"
                            f"{outs_block}"
                            f"{tblgen_block}"
                            f")\n\n")

  def iree_lit_test_suite(self, name, srcs, data, tags=None, **kwargs):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    data_block = _convert_target_list_block("DATA", data)
    labels_block = _convert_string_list_block("LABELS", tags)

    self.converter.body += (f"iree_lit_test_suite(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{data_block}"
                            f"{labels_block}"
                            f")\n\n")

  def iree_check_single_backend_test_suite(self,
                                           name,
                                           srcs,
                                           target_backend,
                                           driver,
                                           compiler_flags=None,
                                           target_backends_and_drivers=None,
                                           runner_args=None,
                                           tags=None,
                                           **kwargs):
    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    target_backend_block = _convert_string_arg_block("TARGET_BACKEND",
                                                     target_backend)
    driver_block = _convert_string_arg_block("DRIVER", driver)
    compiler_flags_block = _convert_string_list_block("COMPILER_FLAGS",
                                                      compiler_flags)
    runner_args_block = _convert_string_list_block("RUNNER_ARGS", runner_args)
    labels_block = _convert_string_list_block("LABELS", tags)

    self.converter.body += (f"iree_check_single_backend_test_suite(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{target_backend_block}"
                            f"{driver_block}"
                            f"{compiler_flags_block}"
                            f"{runner_args_block}"
                            f"{labels_block}"
                            f")\n\n")

  def iree_check_test_suite(self,
                            name,
                            srcs,
                            target_backends_and_drivers=None,
                            compiler_flags=None,
                            runner_args=None,
                            tags=None,
                            **kwargs):
    target_backends = None
    drivers = None
    if target_backends_and_drivers is not None:
      target_backends = [it[0] for it in target_backends_and_drivers]
      drivers = [it[1] for it in target_backends_and_drivers]

    name_block = _convert_string_arg_block("NAME", name, quote=False)
    srcs_block = _convert_srcs_block(srcs)
    target_backends_block = _convert_string_list_block("TARGET_BACKENDS",
                                                       target_backends)
    drivers_block = _convert_string_list_block("DRIVERS", drivers)
    compiler_flags_block = _convert_string_list_block("COMPILER_FLAGS",
                                                      compiler_flags)
    runner_args_block = _convert_string_list_block("RUNNER_ARGS", runner_args)
    labels_block = _convert_string_list_block("LABELS", tags)

    self.converter.body += (f"iree_check_test_suite(\n"
                            f"{name_block}"
                            f"{srcs_block}"
                            f"{target_backends_block}"
                            f"{drivers_block}"
                            f"{compiler_flags_block}"
                            f"{runner_args_block}"
                            f"{labels_block}"
                            f")\n\n")

  def run_binary_test(self, name, test_binary, args=None, data=None):
    if data is not None:
      self._convert_unimplemented_function("iree_run_binary_test",
                                           name + " has data")

    name_block = _convert_string_arg_block("NAME", name)
    test_binary_block = _convert_single_target_block("TEST_BINARY", test_binary)
    args_block = _convert_string_list_block("ARGS", args)
    self.converter.body += (f"iree_run_binary_test(\n"
                            f"{name_block}"
                            f"{args_block}"
                            f"{test_binary_block}"
                            f")\n\n")

  def iree_cmake_extra_content(self, content, inline=False):
    if inline:
      self.converter.body += (f"\n{content}\n")
    else:
      self.converter.header += (f"\n{content}\n")


class Converter(object):
  """Conversion state tracking and full file template substitution."""

  def __init__(self):
    # Header appears after the license block but before `iree_add_all_subdirs`.
    self.header = ""
    # Body appears after `iree_add_all_subdirs`.
    self.body = ""

    self.first_error = None

  def convert(self, copyright_line):
    converted_content = (f"{copyright_line}\n"
                         f"{self.apache_license}\n\n"
                         f"{self.header}\n\n"
                         f"iree_add_all_subdirs()\n\n"
                         f"{self.body}")

    # Cleanup newline characters. This is more convenient than ensuring all
    # conversions are careful with where they insert newlines.
    converted_content = converted_content.replace("\n\n\n", "\n")
    converted_content = converted_content.rstrip() + "\n"

    return converted_content

  apache_license = textwrap.dedent("""\
    #
    # Licensed under the Apache License, Version 2.0 (the "License");
    # you may not use this file except in compliance with the License.
    # You may obtain a copy of the License at
    #
    #      https://www.apache.org/licenses/LICENSE-2.0
    #
    # Unless required by applicable law or agreed to in writing, software
    # distributed under the License is distributed on an "AS IS" BASIS,
    # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    # See the License for the specific language governing permissions and
    # limitations under the License.""")


def GetDict(obj):
  ret = {}
  for k in dir(obj):
    if not k.startswith("_"):
      ret[k] = getattr(obj, k)
  return ret


def convert_build_file(build_file_code,
                       copyright_line,
                       allow_partial_conversion=False):
  converter = Converter()
  exec(build_file_code, GetDict(BuildFileFunctions(converter)))
  converted_text = converter.convert(copyright_line)
  if not allow_partial_conversion and converter.first_error:
    raise converter.first_error  # pylint: disable=raising-bad-type
  return converted_text
