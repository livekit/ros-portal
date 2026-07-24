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

"""Offline tests for the differential-drive odometry math (no hardware needed)."""

import math

from cobra_flex_control.diff_drive import integrate_pose
from cobra_flex_control.diff_drive import wheel_speeds_to_body_twist

TRACK_WIDTH = 0.228


def test_equal_wheels_is_pure_translation():
    v, w = wheel_speeds_to_body_twist(0.2, 0.2, TRACK_WIDTH)
    assert math.isclose(v, 0.2)
    assert w == 0.0


def test_opposite_wheels_is_pure_rotation():
    v, w = wheel_speeds_to_body_twist(-0.1, 0.1, TRACK_WIDTH)
    assert v == 0.0
    assert math.isclose(w, 0.2 / TRACK_WIDTH)
    assert w > 0.0  # right side forward -> positive (CCW) yaw


def test_straight_integration():
    x, y, yaw = integrate_pose(0.0, 0.0, math.pi / 2.0, 1.0, 0.0, 0.5)
    assert math.isclose(x, 0.0, abs_tol=1e-12)
    assert math.isclose(y, 0.5)
    assert math.isclose(yaw, math.pi / 2.0)


def test_quarter_circle_arc():
    # v = r*w with r=1: a quarter turn should land at (1, 1) facing +y.
    w = math.pi / 2.0
    x, y, yaw = integrate_pose(0.0, 0.0, 0.0, w * 1.0, w, 1.0)
    assert math.isclose(x, 1.0)
    assert math.isclose(y, 1.0)
    assert math.isclose(yaw, math.pi / 2.0)


def test_in_place_rotation_moves_nothing():
    x, y, yaw = integrate_pose(1.0, 2.0, 0.0, 0.0, 1.0, 0.25)
    assert math.isclose(x, 1.0)
    assert math.isclose(y, 2.0)
    assert math.isclose(yaw, 0.25)
