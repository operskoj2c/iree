#!/usr/bin/env python3
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
"""This script assists with converting from Bazel BUILD files to CMakeLists.txt.

Bazel BUILD files should, where possible, be written to use simple features
that can be directly evaluated and avoid more advanced features like
variables, list comprehensions, etc.

Generated CMake files will be similar in structure to their source BUILD
files by using the functions in build_tools/cmake/ that imitate corresponding
Bazel rules (e.g. cc_library -> iree_cc_library.cmake).

For usage, see:
  python3 build_tools/bazel_to_cmake/bazel_to_cmake.py --help
"""
# pylint: disable=missing-docstring

import argparse
import datetime
import os
import re
import sys
import textwrap
from enum import Enum

import bazel_to_cmake_converter

repo_root = None

EDIT_BLOCKING_PATTERN = re.compile(
    r"bazel[\s_]*to[\s_]*cmake[\s_]*:?[\s_]*do[\s_]*not[\s_]*edit",
    flags=re.IGNORECASE)

PRESERVE_TAG = "### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###"


class Status(Enum):
  UPDATED = 1
  NOOP = 2
  FAILED = 3
  SKIPPED = 4
  NO_BUILD_FILE = 5


def parse_arguments():
  global repo_root

  parser = argparse.ArgumentParser(
      description="Bazel to CMake conversion helper.")
  parser.add_argument("--preview",
                      help="Prints results instead of writing files",
                      action="store_true",
                      default=False)
  parser.add_argument(
      "--allow_partial_conversion",
      help="Generates partial files, ignoring errors during conversion.",
      action="store_true",
      default=False)
  parser.add_argument(
      "--verbosity",
      "-v",
      type=int,
      default=0,
      help="Specify verbosity level where higher verbosity emits more logging."
      " 0 (default): Only output errors and summary statistics."
      " 1: Also output the name of each directory as it's being processed and"
      " whether the directory is skipped."
      " 2: Also output when conversion was successful.")

  # Specify only one of these (defaults to --root_dir=iree).
  group = parser.add_mutually_exclusive_group()
  group.add_argument("--dir",
                     help="Converts the BUILD file in the given directory",
                     default=None)
  group.add_argument(
      "--root_dir",
      help="Converts all BUILD files under a root directory (defaults to iree/)",
      default="iree")

  args = parser.parse_args()

  # --dir takes precedence over --root_dir.
  # They are mutually exclusive, but the default value is still set.
  if args.dir:
    args.root_dir = None

  return args


def setup_environment():
  """Sets up some environment globals."""
  global repo_root

  # Determine the repository root (two dir-levels up).
  repo_root = os.path.dirname(
      os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def repo_relpath(path):
  return os.path.relpath(path, repo_root)


def log(string, *args, indent=0, **kwargs):
  print(textwrap.indent(string, prefix=(indent * " ")),
        *args,
        **kwargs,
        file=sys.stderr)


def convert_directories(directories, write_files, allow_partial_conversion,
                        verbosity):
  failure_dirs = []
  skip_count = 0
  success_count = 0
  noop_count = 0
  for directory in directories:
    status = convert_directory(
        directory,
        write_files=write_files,
        allow_partial_conversion=allow_partial_conversion,
        verbosity=verbosity)
    if status == Status.FAILED:
      failure_dirs.append(repo_relpath(directory))
    elif status == Status.SKIPPED:
      skip_count += 1
    elif status == Status.UPDATED:
      success_count += 1
    elif status == Status.NOOP:
      noop_count += 1

  log(f"{success_count} CMakeLists.txt files were updated, {skip_count} were"
      f" skipped, and {noop_count} required no change.")
  if failure_dirs:
    log(f"ERROR: Encountered unexpected errors converting {len(failure_dirs)}"
        " directories:")
    log("\n".join(failure_dirs), indent=2)
    sys.exit(1)


def convert_directory(directory_path, write_files, allow_partial_conversion,
                      verbosity):
  if not os.path.isdir(directory_path):
    raise FileNotFoundError(f"Cannot find directory '{directory_path}'")

  rel_dir_path = repo_relpath(directory_path)
  if verbosity >= 1:
    log(f"Processing {rel_dir_path}")

  build_file_path = os.path.join(directory_path, "BUILD")
  cmakelists_file_path = os.path.join(directory_path, "CMakeLists.txt")

  rel_cmakelists_file_path = repo_relpath(cmakelists_file_path)
  rel_build_file_path = repo_relpath(build_file_path)

  if not os.path.isfile(build_file_path):
    return Status.NO_BUILD_FILE

  autogeneration_tag = f"Autogenerated by {repo_relpath(os.path.abspath(__file__))}"

  header = "\n".join(["#" * 80] + [
      l.ljust(79) + "#" for l in [
          f"# {autogeneration_tag} from",
          f"# {rel_build_file_path}",
          "#",
          "# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary",
          "# CMake-only content.",
          "#",
          f"# To disable autogeneration for this file entirely, delete this header.",
      ]
  ] + ["#" * 80])

  old_lines = []
  preserved_footer_lines = ["\n" + PRESERVE_TAG + "\n"]
  if os.path.isfile(cmakelists_file_path):
    found_autogeneration_tag = False
    found_preserve_tag = False
    with open(cmakelists_file_path) as f:
      old_lines = f.readlines()

    for line in old_lines:
      if not found_autogeneration_tag and autogeneration_tag in line:
        found_autogeneration_tag = True
      if not found_preserve_tag and PRESERVE_TAG in line:
        found_preserve_tag = True
      elif found_preserve_tag:
        preserved_footer_lines.append(line)
    if not found_autogeneration_tag:
      if verbosity >= 1:
        log(f"Skipped. Did not find autogeneration line.", indent=2)
      return Status.SKIPPED
  preserved_footer = "".join(preserved_footer_lines)

  with open(build_file_path, "rt") as build_file:
    build_file_code = compile(build_file.read(), build_file_path, "exec")
  try:
    converted_build_file = bazel_to_cmake_converter.convert_build_file(
        build_file_code, allow_partial_conversion=allow_partial_conversion)
  except (NameError, NotImplementedError) as e:
    log(
        f"ERROR generating {rel_dir_path}.\n"
        f"Missing a rule handler in bazel_to_cmake_converter.py?\n"
        f"Reason: `{type(e).__name__}: {e}`",
        indent=2)
    return Status.FAILED
  except KeyError as e:
    log(
        f"ERROR generating {rel_dir_path}.\n"
        f"Missing a conversion in bazel_to_cmake_targets.py?\n"
        f"Reason: `{type(e).__name__}: {e}`",
        indent=2)
    return Status.FAILED
  converted_content = header + converted_build_file + preserved_footer
  if write_files:
    with open(cmakelists_file_path, "wt") as cmakelists_file:
      cmakelists_file.write(converted_content)
  else:
    print(converted_content, end="")

  if converted_content == "".join(old_lines):
    if verbosity >= 2:
      log(f"{rel_cmakelists_file_path} required no update", indent=2)
    return Status.NOOP

  if verbosity >= 2:
    log(
        f"Successfly generated {rel_cmakelists_file_path}"
        f" from {rel_build_file_path}",
        indent=2)
  return Status.UPDATED


def main(args):
  """Runs Bazel to CMake conversion."""
  global repo_root

  write_files = not args.preview

  if args.root_dir:
    root_directory_path = os.path.join(repo_root, args.root_dir)
    log(f"Converting directory tree rooted at: {root_directory_path}")
    convert_directories((root for root, _, _ in os.walk(root_directory_path)),
                        write_files=write_files,
                        allow_partial_conversion=args.allow_partial_conversion,
                        verbosity=args.verbosity)
  elif args.dir:
    convert_directories([os.path.join(repo_root, args.dir)],
                        write_files=write_files,
                        allow_partial_conversion=args.allow_partial_conversion,
                        verbosity=args.verbosity)


if __name__ == "__main__":
  setup_environment()
  main(parse_arguments())
