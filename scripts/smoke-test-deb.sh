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

readonly ros_distro="${ROS_DISTRO:-}"
readonly deb_path="${1:-}"

if [[ -z "${ros_distro}" || -z "${deb_path}" ]]; then
  echo "Usage: ROS_DISTRO=<distro> $0 /path/to/package.deb" >&2
  exit 2
fi

readonly expected_package="ros-${ros_distro}-livekit-bridge"
actual_package="$(dpkg-deb --field "${deb_path}" Package)"
if [[ "${actual_package}" != "${expected_package}" ]]; then
  echo "Unexpected Debian package name: ${actual_package}" >&2
  exit 1
fi

apt-get update
apt-get install -y "${deb_path}"

dpkg-query --show --showformat='${Status}\n' "${expected_package}" |
  grep -qx "install ok installed"

readonly install_prefix="/opt/livekit/ros/${ros_distro}"
set +u
source "${install_prefix}/setup.bash"
set -u

readonly bridge_prefix="${install_prefix}/ros2_livekit_bridge"
actual_prefix="$(ros2 pkg prefix ros2_livekit_bridge)"
if [[ "${actual_prefix}" != "${bridge_prefix}" ]]; then
  echo "Unexpected bridge prefix: ${actual_prefix}" >&2
  exit 1
fi

readonly bridge_node="${bridge_prefix}/lib/ros2_livekit_bridge/ros2_livekit_bridge_node"
if [[ ! -x "${bridge_node}" ]]; then
  echo "Bridge node is missing: ${bridge_node}" >&2
  exit 1
fi

if ldd "${bridge_node}" | awk '/not found/ { found = 1 } END { exit !found }'; then
  ldd "${bridge_node}" >&2
  echo "Bridge node has unresolved shared libraries" >&2
  exit 1
fi

# Verify the installed launch description can be resolved and parsed.
ros2 launch ros2_livekit_bridge livekit_bridge.launch.py --show-args >/dev/null
"livekit-ros2-bridge-${ros_distro}" --show-args >/dev/null

readonly smoke_config="$(mktemp)"
readonly bridge_log="$(mktemp)"
trap 'rm -f "${smoke_config}" "${bridge_log}"' EXIT
cat >"${smoke_config}" <<'EOF'
ros2_livekit_bridge:
  version: "0.0.1"
EOF

export LIVEKIT_URL="${LIVEKIT_URL:-ws://127.0.0.1:7880}"
export LIVEKIT_TOKEN="${LIVEKIT_TOKEN:-$(
  python3 - <<'PY'
import base64
import hashlib
import hmac
import json
import time

def encode(value):
    return base64.urlsafe_b64encode(json.dumps(value, separators=(",", ":")).encode()).rstrip(b"=")

header = encode({"alg": "HS256", "typ": "JWT"})
payload = encode({
    "iss": "devkey",
    "sub": "debian-smoke-test",
    "nbf": int(time.time()) - 1,
    "exp": int(time.time()) + 60,
    "video": {
        "room": "ros2_livekit_bridge_debian_smoke",
        "roomJoin": True,
        "canPublish": True,
        "canSubscribe": True,
        "canPublishData": True,
    },
})
signature = hmac.new(b"secret", header + b"." + payload, hashlib.sha256).digest()
print((header + b"." + payload + b"." + base64.urlsafe_b64encode(signature).rstrip(b"=")).decode())
PY
)}"

set +e
timeout 10s "${bridge_node}" --ros-args -p "config_path:=${smoke_config}" >"${bridge_log}" 2>&1
readonly bridge_status=$?
set -e
cat "${bridge_log}"
if [[ "${bridge_status}" -ne 124 ]]; then
  echo "Bridge exited before the 10-second smoke-test timeout (status ${bridge_status})" >&2
  exit "${bridge_status}"
fi
if ! grep -q "Connected to LiveKit room" "${bridge_log}"; then
  echo "Bridge did not connect to the LiveKit smoke-test server" >&2
  exit 1
fi

echo "Verified ${deb_path} on ROS ${ros_distro}"
