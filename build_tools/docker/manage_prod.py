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
"""Uses prod_digests.txt to update GCR's :prod tags.

Usage:
  Pull all images that should have :prod tags, tag the with :prod and push
  them to GCR. This will make sure that you are at upstream head on the main
  branch before pushing:
    python3 build_tools/docker/manage_prod.py

  Pull all images that should have :prod tags:
    python3  build_tools/docker/manage_prod.py --pull_only
"""

import argparse
import os
import utils


def parse_arguments():
  """Parses command-line options."""
  parser = argparse.ArgumentParser(
      description="Pull and push the images in prod_digests.txt to GCR.")
  parser.add_argument("--pull_only",
                      "--pull-only",
                      action="store_true",
                      help="Pull but do not tag or push the images.")
  return parser.parse_args()


if __name__ == "__main__":
  args = parse_arguments()

  if not args.pull_only:
    # Ensure the user has the correct authorization if they try to push to GCR.
    utils.check_gcloud_auth()

    # Only allow the :prod tag to be pushed from the version of
    # `prod_digests.txt` at upstream HEAD on the main branch.
    utils.run_command([os.path.normpath("scripts/git/git_update.sh"), "main"])

  with open(utils.PROD_DIGESTS_PATH, "r") as f:
    images_with_digests = [line.strip() for line in f.readlines()]

  for image_with_digest in images_with_digests:
    image_url, _ = image_with_digest.split("@")
    tagged_image_url = f"{image_url}:prod"

    utils.run_command(["docker", "pull", image_with_digest])
    if not args.pull_only:
      utils.run_command(["docker", "tag", image_with_digest, tagged_image_url])
      utils.run_command(["docker", "push", tagged_image_url])
