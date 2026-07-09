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
#
# libcamera enumerates cameras through udev. Containers frequently start without
# a running udev daemon, leaving /run/udev empty; libcamera then reports
# "no cameras available" even though the sensor and media graph are fine.
#
# This starts udevd (if not already running) and populates the device database.
# Run it once per container boot, as root, before launching the camera.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "setup_camera_udev.sh must run as root" >&2
  exit 1
fi

if [ ! -S /run/udev/control ]; then
  echo "Starting systemd-udevd..."
  /lib/systemd/systemd-udevd --daemon
else
  echo "udevd already running."
fi

udevadm trigger
udevadm settle --timeout=10

echo "udev ready. Cameras visible to V4L2:"
if command -v v4l2-ctl >/dev/null 2>&1; then
  v4l2-ctl --list-devices 2>/dev/null | sed 's/^/  /' || true
fi
