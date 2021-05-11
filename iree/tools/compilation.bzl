# Copyright 2019 Google LLC
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

"""Rules for compiling IREE executables, modules, and archives."""

load("//build_tools/embed_data:build_defs.bzl", "c_embed_data")

# TODO(benvanik): port to a full starlark rule, document, etc.
def iree_bytecode_module(
        name,
        src,
        flags = ["-iree-mlir-to-vm-bytecode-module"],
        translate_tool = "//iree/tools:iree-translate",
        c_identifier = "",
        **kwargs):
    native.genrule(
        name = name,
        srcs = [src],
        outs = [
            "%s.vmfb" % (name),
        ],
        cmd = " && ".join([
            " ".join([
                "$(location %s)" % (translate_tool),
                " ".join(flags),
                "-o $(location %s.vmfb)" % (name),
                "$(location %s)" % (src),
            ]),
        ]),
        tools = [translate_tool],
        message = "Compiling IREE module %s..." % (name),
        output_to_bindir = 1,
        **kwargs
    )

    # Embed the module for use in C.
    if c_identifier:
        c_embed_data(
            name = "%s_c" % (name),
            identifier = c_identifier,
            srcs = ["%s.vmfb" % (name)],
            c_file_output = "%s_c.c" % (name),
            h_file_output = "%s_c.h" % (name),
            flatten = True,
            **kwargs
        )
