#!/usr/bin/env bash
#
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

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
shared_script="${repo_root}/src/externals/cpp-tools/clang-tidy.sh"
config="${repo_root}/.clang-tidy"
expected_config="src/externals/cpp-tools/.clang-tidy"

if [[ ! -x "${shared_script}" ]]; then
  echo "ERROR: src/externals/cpp-tools/clang-tidy.sh is unavailable." >&2
  echo "Run: mkdir -p src/externals && vcs import src/externals < external.repos" >&2
  exit 1
fi

if [[ ! -L "${config}" ]] || [[ "$(readlink "${config}")" != "${expected_config}" ]]; then
  echo "ERROR: the project .clang-tidy link is not installed." >&2
  echo "Run: ./src/externals/cpp-tools/install.sh clang-tidy --repo-root \"${repo_root}\"" >&2
  exit 1
fi

exec "${shared_script}" \
  --repo-root "${repo_root}" \
  --build-dir build \
  --file-regex '^.*/src/(ros_portal|ros_portal_config)/src/.*\.(c|cc|cpp|cxx)$' \
  --header-filter '^.*/src/(ros_portal|ros_portal_config)/(include|src)/.*\.(h|hh|hpp|hxx)$' \
  --exclude-header-filter '(.*/test/.*)|(.*/build/.*)|(.*/install/.*)|(.*/src/externals/.*)' \
  "$@"
