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

readonly expected_package="ros-${ros_distro}-livekit-portal"
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

actual_prefix="$(ros2 pkg prefix ros_portal)"
if [[ "${actual_prefix}" != "${install_prefix}" ]]; then
  echo "Unexpected ROS Portal prefix: ${actual_prefix}" >&2
  exit 1
fi

readonly ros_portal_node="${install_prefix}/lib/ros_portal/ros_portal_node"
if [[ ! -x "${ros_portal_node}" ]]; then
  echo "ROS Portal node is missing: ${ros_portal_node}" >&2
  exit 1
fi

if ldd "${ros_portal_node}" | awk '/not found/ { found = 1 } END { exit !found }'; then
  ldd "${ros_portal_node}" >&2
  echo "ROS Portal node has unresolved shared libraries" >&2
  exit 1
fi

# Verify the installed launch description can be resolved and parsed without
# starting ROS Portal or requiring a LiveKit server.
ros2 launch ros_portal ros_portal.launch.py --show-args >/dev/null
"ros-portal-${ros_distro}" --show-args >/dev/null

echo "Verified ${deb_path} on ROS ${ros_distro}"
