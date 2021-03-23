#!/usr/bin/env python3
# Copyright 2021 Google LLC
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

# Benchmarks modules generated by compile_android_modules.py and writes
# performance data to a text proto file, e.g., mako-phone-mako_tag-git_hash.
#
# The script benchmarks modules on 7-th big core, ie, run with `taskset 80`.

import argparse
import datetime
import subprocess
import os
import re

import configuration

PATTERN = re.compile(r"BM_(\w+)(.+)/real_time(\s+) (?P<ms>[.0-9]+) ms")
DEVICE_ROOT = "/data/local/tmp/benchmark_tmpdir"


def parse_arguments():
  parser = argparse.ArgumentParser()
  parser.add_argument("--git_hash", default="UNKNOWN")
  parser.add_argument("phone")
  args = parser.parse_args()
  return args


def get_mako_sample(value, tag) -> str:
  return f"""
samples: {{
  time: {value}
  target: "{tag}"
}}""".strip()


def get_mako_metadata(git_hash, timestamp, benchmark_key) -> str:
  return f"""
metadata: {{
  git_hash: "{git_hash}"
  timestamp_ms: {timestamp}
  benchmark_key: "{benchmark_key}"
}}
""".strip()


def benchmark(module_name, flagfile_name, target) -> str:
  samples = []
  driver = target.get_driver()
  cmd = [
      "adb", "shell", "LD_LIBRARY_PATH=/data/local/tmp", "taskset", "80",
      f"{DEVICE_ROOT}/iree-benchmark-module",
      f"--flagfile={DEVICE_ROOT}/{flagfile_name}",
      f"--module_file={DEVICE_ROOT}/{module_name}", f"--driver={driver}",
      "--benchmark_repetitions=10"
  ] + target.runtime_flags
  print(f"Running cmd: {' '.join(cmd)}")
  output = subprocess.run(cmd,
                          check=True,
                          capture_output=True,
                          universal_newlines=True).stdout
  for line in output.split("\n"):
    m = PATTERN.match(line)
    if m is not None:
      samples.append(get_mako_sample(m.group("ms"), target.mako_tag))
  return "\n".join(samples)


def main(args) -> None:
  timestamp = int(datetime.datetime.now().timestamp() * 1000)
  for model_benchmark in configuration.MODEL_BENCHMARKS:
    for phone in model_benchmark.phones:
      if phone.name != args.phone:
        continue
      mako_log = []
      for target in phone.targets:
        module_name = configuration.get_module_name(model_benchmark.name,
                                                    phone.name, target.mako_tag)
        flagfile_name = configuration.get_flagfile_name(model_benchmark.name)
        mako_log.append(benchmark(module_name, flagfile_name, target))
      mako_log.append(
          get_mako_metadata(args.git_hash, timestamp, phone.benchmark_key))
      mako_log = "\n".join(mako_log)
      filename = f"mako-{model_benchmark.name}-{phone.name}-{args.git_hash}.log"
      open(filename, "w").write(mako_log)
      print(mako_log)


if __name__ == "__main__":
  main(parse_arguments())
