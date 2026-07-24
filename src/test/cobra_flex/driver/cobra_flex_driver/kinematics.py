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

"""Pure differential-drive conversions for the Cobra Flex serial protocol.

Kept free of ROS imports so the twist -> wheel-command mapping is unit-testable
offline (the Jetson is not wired to the chassis yet).

The Cobra Flex hub motors are closed-loop (FOC), so unlike the WAVE ROVER the
serial ``L``/``R`` values are true wheel speeds, in units of 0.1 rpm
(range -1800..1800 per the wiki, i.e. +-180 rpm).
"""

import math

# Serial speed unit: the firmware takes wheel speeds in 0.1 rpm increments.
TENTH_RPM_PER_RAD_S = 10.0 * 60.0 / (2.0 * math.pi)


def twist_to_wheel_tenth_rpm(linear_x: float, angular_z: float, wheel_radius: float,
                             track_width: float, max_tenth_rpm: float) -> tuple:
    """Map a body twist to (left, right) wheel commands in 0.1 rpm units.

    Standard differential-drive kinematics; both sides of the 4WD chassis are
    commanded together (the firmware's ``{"T":1}`` command is per side). If
    either side exceeds ``max_tenth_rpm`` both are scaled down together so the
    commanded turn ratio is preserved.
    """
    v_left = linear_x - angular_z * track_width / 2.0
    v_right = linear_x + angular_z * track_width / 2.0
    left = v_left / wheel_radius * TENTH_RPM_PER_RAD_S
    right = v_right / wheel_radius * TENTH_RPM_PER_RAD_S
    peak = max(abs(left), abs(right))
    if peak > max_tenth_rpm > 0.0:
        scale = max_tenth_rpm / peak
        left *= scale
        right *= scale
    return left, right


def tenth_rpm_to_rad_s(tenth_rpm: float) -> float:
    """Convert a feedback wheel speed (0.1 rpm units) to rad/s."""
    return tenth_rpm / TENTH_RPM_PER_RAD_S
