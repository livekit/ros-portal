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

from livekit_scarf_slam.transforms import BODY_TO_OPTICAL_ROTATION
from livekit_scarf_slam.transforms import make_transform
from livekit_scarf_slam.transforms import matrix_from_quaternion
from livekit_scarf_slam.transforms import pose_from_transform
from livekit_scarf_slam.transforms import quaternion_from_matrix
from livekit_scarf_slam.transforms import transform_from_pose
import numpy as np
import pytest


def test_body_to_optical_is_the_ros_optical_rotation():
    np.testing.assert_allclose(
        quaternion_from_matrix(BODY_TO_OPTICAL_ROTATION), [-0.5, 0.5, -0.5, 0.5], atol=1e-12)
    # The optical z axis (forward) is the body x axis.
    np.testing.assert_allclose(BODY_TO_OPTICAL_ROTATION @ [0, 0, 1], [1, 0, 0])


@pytest.mark.parametrize('seed', range(20))
def test_quaternion_round_trip(seed):
    rng = np.random.default_rng(seed)
    q = rng.normal(size=4)
    q /= np.linalg.norm(q)
    q = q if q[3] >= 0 else -q
    rotation = matrix_from_quaternion(q)
    np.testing.assert_allclose(rotation @ rotation.T, np.eye(3), atol=1e-12)
    np.testing.assert_allclose(quaternion_from_matrix(rotation), q, atol=1e-12)


def test_pose_round_trip():
    transform = make_transform(matrix_from_quaternion([0.1, -0.2, 0.3, 0.9]), [1.0, 2.0, 3.0])
    position, orientation = pose_from_transform(transform)
    np.testing.assert_allclose(transform_from_pose(position, orientation), transform, atol=1e-12)
