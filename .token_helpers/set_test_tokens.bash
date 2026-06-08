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

# Generate two LiveKit access tokens via `lk` and set the environment variables
# required by the ROS bridge participant-ID integration test.
#
#   source .token_helpers/set_test_tokens.bash
#   eval "$(bash .token_helpers/set_test_tokens.bash)"
#
# Exports:
#   LIVEKIT_URL
#   LIVEKIT_TOKEN_A
#   LIVEKIT_TOKEN_B

_sourced=0
if [[ -n "${BASH_VERSION:-}" && "${BASH_SOURCE[0]}" != "${0}" ]]; then
  _sourced=1
elif [[ -n "${ZSH_VERSION:-}" && "${ZSH_EVAL_CONTEXT:-}" == *:file* ]]; then
  _sourced=1
fi

_fail() {
  echo "set_test_tokens.bash: $1" >&2
  if [[ "$_sourced" -eq 1 ]]; then
    return "${2:-1}"
  fi
  exit "${2:-1}"
}

if [[ "$_sourced" -eq 0 ]]; then
  set -euo pipefail
fi

if [[ $# -ne 0 ]]; then
  _fail "this script is configured through environment variables and does not accept arguments" 2
fi

LIVEKIT_API_KEY="${LIVEKIT_API_KEY:-devkey}"
LIVEKIT_API_SECRET="${LIVEKIT_API_SECRET:-secret}"
LIVEKIT_ROOM="${LIVEKIT_ROOM:-ros_bridge_participant_id_test}"
LIVEKIT_VALID_FOR="${LIVEKIT_VALID_FOR:-99999h}"
LIVEKIT_IDENTITY_A="${LIVEKIT_IDENTITY_A:-bridge-test-a}"
LIVEKIT_IDENTITY_B="${LIVEKIT_IDENTITY_B:-bridge-test-b}"
_grant_json='{"canPublish":true,"canSubscribe":true,"canPublishData":true}'

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

if ! command -v lk >/dev/null 2>&1; then
  _fail "'lk' CLI not found. Install: https://docs.livekit.io/home/cli/" 2
fi

_create_token() {
  local identity="$1"
  local output=""

  output="$(
    lk token create \
      --api-key "$LIVEKIT_API_KEY" \
      --api-secret "$LIVEKIT_API_SECRET" \
      --identity "$identity" \
      --name "$identity" \
      --join \
      --valid-for "$LIVEKIT_VALID_FOR" \
      --room "$LIVEKIT_ROOM" \
      --grant "$_grant_json" \
      --token-only
  )"

  if [[ -z "$output" ]]; then
    _fail "lk token create produced an empty token for identity '$identity'" 1
  fi

  printf '%s' "$output"
}

LIVEKIT_TOKEN_A="$(_create_token "$LIVEKIT_IDENTITY_A")"
LIVEKIT_TOKEN_B="$(_create_token "$LIVEKIT_IDENTITY_B")"

_apply() {
  export LIVEKIT_URL
  export LIVEKIT_TOKEN_A
  export LIVEKIT_TOKEN_B
}

_emit_eval() {
  printf 'export LIVEKIT_URL=%q\n' "$LIVEKIT_URL"
  printf 'export LIVEKIT_TOKEN_A=%q\n' "$LIVEKIT_TOKEN_A"
  printf 'export LIVEKIT_TOKEN_B=%q\n' "$LIVEKIT_TOKEN_B"
  printf 'export LIVEKIT_ROOM=%q\n' "$LIVEKIT_ROOM"
}

if [[ "$_sourced" -eq 1 ]]; then
  _apply
  echo "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B set for this shell." >&2
else
  _emit_eval
  echo "set_test_tokens.bash: for this shell run: source $0   or: eval \"\$(bash $0)\"" >&2
fi
