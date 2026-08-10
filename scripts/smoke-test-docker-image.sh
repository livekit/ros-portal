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
readonly expected_distro="${2:-}"
readonly expected_arch="${3:-}"

if [[ -z "${image_ref}" || -z "${expected_distro}" || -z "${expected_arch}" ]]; then
  echo "Usage: $0 <image> <ros-distro> <amd64|arm64>" >&2
  exit 2
fi

case "${expected_arch}" in
  amd64 | arm64) ;;
  *)
    echo "Unsupported architecture: ${expected_arch}" >&2
    exit 2
    ;;
esac

actual_arch="$(docker image inspect --format '{{.Architecture}}' "${image_ref}")"
if [[ "${actual_arch}" != "${expected_arch}" ]]; then
  echo "Expected image architecture ${expected_arch}, got ${actual_arch}" >&2
  exit 1
fi

actual_distro="$(
  docker image inspect \
    --format '{{index .Config.Labels "io.livekit.ros-distro"}}' \
    "${image_ref}"
)"
if [[ "${actual_distro}" != "${expected_distro}" ]]; then
  echo "Expected ROS distro ${expected_distro}, got ${actual_distro}" >&2
  exit 1
fi

actual_prefix="$(docker run --rm "${image_ref}" ros2 pkg prefix ros_portal)"
expected_prefix="/opt/livekit/ros/${expected_distro}/ros_portal"
if [[ "${actual_prefix}" != "${expected_prefix}" ]]; then
  echo "Expected ROS Portal prefix ${expected_prefix}, got ${actual_prefix}" >&2
  exit 1
fi

echo "======== Verifying video-source dependencies in ${image_ref} ========"
docker run --rm "${image_ref}" bash -c '
  set -euo pipefail
  gst-inspect-1.0 appsink >/dev/null
  gst-inspect-1.0 h264parse >/dev/null
  gst-inspect-1.0 v4l2src >/dev/null
  gst-inspect-1.0 x264enc >/dev/null
  # Overlay elements come from the Pango plugin, which Ubuntu ships in
  # gstreamer1.0-x. That package is installed by the runtime Dockerfile rather
  # than depended on by the Debian package, so this check covers the image only.
  gst-inspect-1.0 clockoverlay >/dev/null
  gst-inspect-1.0 timeoverlay >/dev/null
  gst-launch-1.0 --quiet videotestsrc num-buffers=1 ! fakesink
  # Exercise the tutorial pipeline shape end to end so a missing element in any
  # of the plugin sets fails here rather than at runtime on a robot.
  gst-launch-1.0 --quiet videotestsrc num-buffers=1 \
    ! videoconvert \
    ! timeoverlay \
    ! clockoverlay \
    ! x264enc \
    ! h264parse \
    ! fakesink
'
echo "======== Verified video-source dependencies in ${image_ref} ========"

echo "======== Resolving the ROS Portal launch description in ${image_ref} ========"
docker run --rm "${image_ref}" \
  ros2 launch ros_portal ros_portal.launch.py --show-args
echo "======== Resolved the ROS Portal launch description in ${image_ref} ========"

echo "Verified ${image_ref} for ROS ${expected_distro} on ${expected_arch}"
