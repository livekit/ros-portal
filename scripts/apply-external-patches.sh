#!/usr/bin/env bash

# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly repo_root
readonly medkit_root="${repo_root}/src/externals/ros2_medkit"
readonly medkit_revision="e9697424c8e649a94c75678aaf26db2c00e48db2"
readonly medkit_patch="${repo_root}/patches/ros2_medkit/0001-support-rosidl-buffer-uint8-sequences.patch"

if [[ ! -d "${medkit_root}/.git" ]]; then
  echo "ros2_medkit is missing; run: vcs import --recursive src/externals < external.repos" >&2
  exit 2
fi

actual_revision="$(git -C "${medkit_root}" rev-parse HEAD)"
if [[ "${actual_revision}" != "${medkit_revision}" ]]; then
  echo "ros2_medkit is at ${actual_revision}; expected ${medkit_revision}" >&2
  echo "Update external.repos and the downstream patch together." >&2
  exit 2
fi

if git -C "${medkit_root}" apply --reverse --check "${medkit_patch}" 2>/dev/null; then
  echo "ros2_medkit patches are already applied"
elif git -C "${medkit_root}" apply --check "${medkit_patch}"; then
  git -C "${medkit_root}" apply "${medkit_patch}"
  echo "Applied ros2_medkit patches"
else
  echo "ros2_medkit patch does not apply cleanly" >&2
  exit 1
fi
