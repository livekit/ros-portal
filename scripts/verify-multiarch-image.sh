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

readonly image_ref="${1:-}"
if [[ -z "${image_ref}" ]]; then
  echo "Usage: $0 <multi-architecture-image>" >&2
  exit 2
fi

raw_manifest="$(docker buildx imagetools inspect --raw "${image_ref}")"

for architecture in amd64 arm64; do
  if ! jq -e \
    --arg architecture "${architecture}" \
    '.manifests[] | select(
      .platform.os == "linux" and
      .platform.architecture == $architecture
    )' <<<"${raw_manifest}" >/dev/null; then
    echo "${image_ref} does not contain linux/${architecture}" >&2
    exit 1
  fi
done

echo "Verified ${image_ref} contains linux/amd64 and linux/arm64"
