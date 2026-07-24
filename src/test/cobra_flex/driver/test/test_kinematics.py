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

"""Offline tests for the twist <-> wheel-speed conversions (no hardware needed)."""

import math

from cobra_flex_driver.kinematics import TENTH_RPM_PER_RAD_S
from cobra_flex_driver.kinematics import tenth_rpm_to_rad_s
from cobra_flex_driver.kinematics import twist_to_wheel_tenth_rpm

# Cobra Flex spec-sheet geometry.
WHEEL_RADIUS = 0.03725
TRACK_WIDTH = 0.228
MAX_TENTH_RPM = 1800.0


def test_straight_line_matches_wheel_speed():
    left, right = twist_to_wheel_tenth_rpm(0.1, 0.0, WHEEL_RADIUS, TRACK_WIDTH, MAX_TENTH_RPM)
    assert left == right
    expected = 0.1 / WHEEL_RADIUS * TENTH_RPM_PER_RAD_S
    assert math.isclose(left, expected)
    # 0.1 m/s on a 74.5 mm wheel is ~25.6 rpm -> 256 in 0.1 rpm units.
    assert math.isclose(left, 256.4, rel_tol=1e-3)


def test_in_place_rotation_is_antisymmetric():
    left, right = twist_to_wheel_tenth_rpm(0.0, 1.0, WHEEL_RADIUS, TRACK_WIDTH, MAX_TENTH_RPM)
    assert math.isclose(left, -right)
    assert right > 0.0  # positive yaw -> right side forward


def test_zero_twist_is_zero():
    assert twist_to_wheel_tenth_rpm(0.0, 0.0, WHEEL_RADIUS, TRACK_WIDTH,
                                    MAX_TENTH_RPM) == (0.0, 0.0)


def test_saturation_preserves_turn_ratio():
    # Well past the 180 rpm cap, with a turn mixed in.
    left, right = twist_to_wheel_tenth_rpm(2.0, 4.0, WHEEL_RADIUS, TRACK_WIDTH, MAX_TENTH_RPM)
    raw_left, raw_right = twist_to_wheel_tenth_rpm(2.0, 4.0, WHEEL_RADIUS, TRACK_WIDTH, 0.0)
    assert max(abs(left), abs(right)) <= MAX_TENTH_RPM + 1e-9
    assert math.isclose(left / right, raw_left / raw_right)


def test_feedback_round_trip():
    rad_s = 2.5
    assert math.isclose(tenth_rpm_to_rad_s(rad_s * TENTH_RPM_PER_RAD_S), rad_s)
