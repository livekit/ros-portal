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

set -eo pipefail

readonly ros_distro="${ROS_DISTRO:?ROS_DISTRO is not set}"
readonly ros_setup="/opt/ros/${ros_distro}/setup.bash"
readonly portal_setup="/opt/livekit/ros/${ros_distro}/setup.bash"

if [[ ! -f "${ros_setup}" ]]; then
  echo "ROS ${ros_distro} setup is missing: ${ros_setup}" >&2
  exit 1
fi
if [[ ! -f "${portal_setup}" ]]; then
  echo "ROS Portal setup is missing: ${portal_setup}" >&2
  exit 1
fi

# ROS-generated setup files can read unset variables.
set +u
# shellcheck disable=SC1090
source "${ros_setup}"
# shellcheck disable=SC1090
source "${portal_setup}"
set -u

exec "$@"
