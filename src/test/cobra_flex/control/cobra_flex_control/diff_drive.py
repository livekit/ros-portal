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

"""Pure differential-drive odometry integration, ROS-free for offline testing."""

import math


def wheel_speeds_to_body_twist(v_left: float, v_right: float, track_width: float) -> tuple:
    """Per-side ground speeds (m/s) -> body (linear m/s, angular rad/s)."""
    return (v_right + v_left) / 2.0, (v_right - v_left) / track_width


def integrate_pose(x: float, y: float, yaw: float, v: float, w: float, dt: float) -> tuple:
    """Advance a planar pose by a body twist held for ``dt`` seconds.

    Uses the exact arc solution when turning; falls back to the straight-line
    limit for near-zero angular rates to avoid dividing by ~0.
    """
    if abs(w) < 1e-9:
        return (x + v * math.cos(yaw) * dt,
                y + v * math.sin(yaw) * dt,
                yaw)
    new_yaw = yaw + w * dt
    radius = v / w
    return (x + radius * (math.sin(new_yaw) - math.sin(yaw)),
            y - radius * (math.cos(new_yaw) - math.cos(yaw)),
            new_yaw)
