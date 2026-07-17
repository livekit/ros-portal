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

readonly REPOSITORY="livekit/client-sdk-cpp"
readonly RUN_ID="29618348890"
readonly SDK_COMMIT="80e91b15bc3c719706f50c51d07bb935f8b9a4cb"
readonly ARTIFACT_EXPIRES_AT="2026-07-24T22:49:17Z"
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly CACHE_ROOT="${LIVEKIT_ARTIFACT_CACHE_DIR:-${ROOT}/.cache/livekit-sdk}"

usage() {
  cat <<EOF
Usage: $0 [--arch x64|arm64]

Downloads the pinned successful Linux SDK artifact from:
  https://github.com/${REPOSITORY}/actions/runs/${RUN_ID}

The artifact expires at ${ARTIFACT_EXPIRES_AT}. GitHub CLI authentication with
Actions read access to ${REPOSITORY} is required.
EOF
}

arch=""
while (($#)); do
  case "$1" in
    --arch)
      if (($# < 2)); then
        echo "error: --arch requires x64 or arm64" >&2
        exit 2
      fi
      arch="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${arch}" ]]; then
  case "$(uname -m)" in
    x86_64|amd64)
      arch="x64"
      ;;
    arm64|aarch64)
      arch="arm64"
      ;;
    *)
      echo "error: unsupported architecture: $(uname -m)" >&2
      exit 1
      ;;
  esac
fi

case "${arch}" in
  x64)
    readonly ARTIFACT_ID="8421568817"
    readonly ARTIFACT_NAME="livekit-sdk-linux-x64"
    readonly ARTIFACT_SHA256="c4d6afc631a0911fcf58472aad83f031b355322f0e39a08343c68133efea5d1a"
    ;;
  arm64)
    readonly ARTIFACT_ID="8421519528"
    readonly ARTIFACT_NAME="livekit-sdk-linux-arm64"
    readonly ARTIFACT_SHA256="248c03d2acd743ae14a5af2866a254166bfd348d3aeed6ef89c3008ce5dac462"
    ;;
  *)
    echo "error: unsupported architecture: ${arch}" >&2
    exit 2
    ;;
esac

readonly SDK_DIR="${CACHE_ROOT}/${SDK_COMMIT}/linux-${arch}"
readonly CURRENT_LINK="${CACHE_ROOT}/current"

valid_sdk() {
  local directory="$1"
  [[ -f "${directory}/include/livekit/data_track_options.h" ]] &&
    [[ -f "${directory}/include/livekit/data_track_schema.h" ]] &&
    [[ -f "${directory}/lib/liblivekit.so" ]] &&
    [[ -f "${directory}/lib/liblivekit_ffi.so" ]]
}

if valid_sdk "${SDK_DIR}"; then
  mkdir -p "${CACHE_ROOT}"
  ln -sfn "${SDK_COMMIT}/linux-${arch}" "${CURRENT_LINK}"
  echo "LiveKit SDK artifact already available: ${SDK_DIR}"
  echo "LIVEKIT_LOCAL_SDK_DIR=${CURRENT_LINK}"
  exit 0
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI (gh) is required" >&2
  exit 1
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "error: authenticate GitHub CLI before downloading the SDK artifact" >&2
  echo "  gh auth login" >&2
  exit 1
fi
if ! command -v unzip >/dev/null 2>&1; then
  echo "error: unzip is required" >&2
  exit 1
fi

mkdir -p "${CACHE_ROOT}/${SDK_COMMIT}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/livekit-sdk-artifact.XXXXXX")"
trap 'rm -rf "${work_dir}"' EXIT
archive="${work_dir}/${ARTIFACT_NAME}.zip"
staging="${work_dir}/sdk"
mkdir -p "${staging}"

echo "Downloading ${ARTIFACT_NAME} from SDK commit ${SDK_COMMIT}"
gh api "repos/${REPOSITORY}/actions/artifacts/${ARTIFACT_ID}/zip" >"${archive}"

if command -v sha256sum >/dev/null 2>&1; then
  actual_sha256="$(sha256sum "${archive}" | awk '{print $1}')"
else
  actual_sha256="$(shasum -a 256 "${archive}" | awk '{print $1}')"
fi
if [[ "${actual_sha256}" != "${ARTIFACT_SHA256}" ]]; then
  echo "error: artifact digest mismatch" >&2
  echo "  expected: ${ARTIFACT_SHA256}" >&2
  echo "  actual:   ${actual_sha256}" >&2
  exit 1
fi

# GitHub ZIP timestamps can appear hours ahead inside Docker Desktop, causing
# repeated CMake rebuilds. Keep extraction-time timestamps instead.
unzip -q -DD "${archive}" -d "${staging}"
if ! valid_sdk "${staging}"; then
  echo "error: downloaded artifact does not contain the expected SDK layout" >&2
  exit 1
fi

rm -rf "${SDK_DIR}"
mv "${staging}" "${SDK_DIR}"
ln -sfn "${SDK_COMMIT}/linux-${arch}" "${CURRENT_LINK}"

echo "Installed ${ARTIFACT_NAME} at ${SDK_DIR}"
echo "LIVEKIT_LOCAL_SDK_DIR=${CURRENT_LINK}"
