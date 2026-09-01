#!/usr/bin/env bash
# Copyright 2026 LiveKit, Inc.
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

# Generate two LiveKit access tokens via `lk` and set the environment variables
# required by ROS Portal's integration tests.
#
#   source scripts/set-test-tokens.sh
#   eval "$(bash scripts/set-test-tokens.sh)"
#
# Exports:
#   LIVEKIT_TOKEN_A
#   LIVEKIT_TOKEN_B
#   LIVEKIT_URL

_sourced=0
if [[ -n "${BASH_VERSION:-}" ]] && [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
  _sourced=1
elif [[ -n "${ZSH_VERSION:-}" && "${ZSH_EVAL_CONTEXT:-}" == *:file* ]]; then
  _sourced=1
fi

_fail() {
  echo "set-test-tokens: $1" >&2
}

if [[ "$_sourced" -eq 0 ]]; then
  set -euo pipefail
fi

LIVEKIT_API_KEY="${LIVEKIT_API_KEY:-devkey}"
LIVEKIT_API_SECRET="${LIVEKIT_API_SECRET:-secret}"
LIVEKIT_ROOM="${LIVEKIT_ROOM:-ros_portal_test_room}"
LIVEKIT_VALID_FOR="${LIVEKIT_VALID_FOR:-99999h}"
LIVEKIT_IDENTITY_A="${LIVEKIT_IDENTITY_A:-ros-portal-test-a}"
LIVEKIT_IDENTITY_B="${LIVEKIT_IDENTITY_B:-ros-portal-test-b}"
if [[ -z "${LIVEKIT_URL:-}" ]]; then
  # Linux CI jobs run LiveKit on the same host as the devcontainer.
  if [[ "${CI:-}" == "true" ]]; then
    LIVEKIT_URL="ws://127.0.0.1:7880"
  # Local Docker Desktop setups typically expose the host via this DNS alias.
  elif command -v getent >/dev/null 2>&1 &&
    getent hosts host.docker.internal >/dev/null 2>&1; then
    LIVEKIT_URL="ws://host.docker.internal:7880"
  else
    LIVEKIT_URL="ws://127.0.0.1:7880"
  fi
fi

if [[ $# -ne 0 ]]; then
  _fail "this script is configured through environment variables and does not accept arguments" 2
  if [[ "$_sourced" -eq 1 ]]; then
    return 2
  fi
  exit 2
fi

_grant_json='{"canPublish":true,"canSubscribe":true,"canPublishData":true}'

if ! command -v lk >/dev/null 2>&1; then
  _fail "'lk' CLI not found. Install: https://docs.livekit.io/home/cli/" 2
  if [[ "$_sourced" -eq 1 ]]; then
    return 2
  fi
  exit 2
fi

_create_token() {
  local identity="$1"
  local output=""
  local command_status=0
  local token=""

  output="$(
    bash -c '
      lk token create \
        --api-key "$1" \
        --api-secret "$2" \
        -i "$3" \
        --join \
        --valid-for "$4" \
        --room "$5" \
        --grant "$6" \
        --allow-update-metadata 2>&1
    ' _ "$LIVEKIT_API_KEY" "$LIVEKIT_API_SECRET" "$identity" \
      "$LIVEKIT_VALID_FOR" "$LIVEKIT_ROOM" "$_grant_json"
  )"
  command_status=$?
  if [[ "$command_status" -ne 0 ]]; then
    echo "$output" >&2
    _fail "lk token create failed for identity '$identity'" 1
    return 1
  fi

  while IFS= read -r line || [[ -n "${line}" ]]; do
    if [[ "$line" == "Access token: "* ]]; then
      token="${line#Access token: }"
      break
    fi
  done <<< "$output"

  if [[ -z "$token" ]]; then
    echo "$output" >&2
    _fail "could not parse Access token for identity '$identity'" 1
    return 1
  fi

  printf '%s' "$token"
}

if ! LIVEKIT_TOKEN_A="$(_create_token "$LIVEKIT_IDENTITY_A")"; then
  if [[ "$_sourced" -eq 1 ]]; then
    return 1
  fi
  exit 1
fi

if ! LIVEKIT_TOKEN_B="$(_create_token "$LIVEKIT_IDENTITY_B")"; then
  if [[ "$_sourced" -eq 1 ]]; then
    return 1
  fi
  exit 1
fi

_apply() {
  export LIVEKIT_TOKEN_A
  export LIVEKIT_TOKEN_B
  export LIVEKIT_URL
}

_emit_eval() {
  printf 'export LIVEKIT_TOKEN_A=%q\n' "$LIVEKIT_TOKEN_A"
  printf 'export LIVEKIT_TOKEN_B=%q\n' "$LIVEKIT_TOKEN_B"
  printf 'export LIVEKIT_URL=%q\n' "$LIVEKIT_URL"
}

if [[ "$_sourced" -eq 1 ]]; then
  _apply
  echo "LIVEKIT_TOKEN_A, LIVEKIT_TOKEN_B, and LIVEKIT_URL set for this shell. using LIVEKIT_ROOM: $LIVEKIT_ROOM" >&2
else
  _emit_eval
  echo "set-test-tokens: for this shell run: source $0   or: eval \"\$(bash $0)\"" >&2
fi
