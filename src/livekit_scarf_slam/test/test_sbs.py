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

import cv2
from livekit_scarf_slam.sbs import decode_grayscale
from livekit_scarf_slam.sbs import frame_scale
from livekit_scarf_slam.sbs import left_eye_jpeg
from livekit_scarf_slam.sbs import split_sbs
import numpy as np
import pytest

SBS_SIZE = (3840, 1080)


def _sbs_jpeg(width, height, left_value=50, right_value=200):
    frame = np.empty((height, width, 3), dtype=np.uint8)
    frame[:, :width // 2] = left_value
    frame[:, width // 2:] = right_value
    ok, data = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 95])
    assert ok
    return data.tobytes()


@pytest.mark.parametrize('reduction', [1, 2, 4])
def test_decode_and_split(reduction):
    frame = decode_grayscale(_sbs_jpeg(*SBS_SIZE), reduction)
    assert frame.shape == (1080 // reduction, 3840 // reduction)
    left, right = split_sbs(frame)
    assert left.shape == right.shape == (1080 // reduction, 1920 // reduction)
    # Skip the columns next to the seam, where JPEG blocks mix both values.
    assert abs(float(left[:, :-16].mean()) - 50) < 3
    assert abs(float(right[:, 16:].mean()) - 200) < 3


def test_decode_rejects_bad_input():
    assert decode_grayscale(b'not a jpeg') is None
    with pytest.raises(ValueError):
        decode_grayscale(_sbs_jpeg(64, 32), 3)


@pytest.mark.parametrize('frame_size, expected', [
    ((3840, 1080), 1.0),
    ((1920, 540), 0.5),
    ((1280, 360), 1.0 / 3.0),
    ((1920, 1080), None),
    ((3840, 1088), None),
])
def test_frame_scale(frame_size, expected):
    scale = frame_scale(frame_size, SBS_SIZE)
    if expected is None:
        assert scale is None
    else:
        assert scale == pytest.approx(expected)


@pytest.mark.parametrize('frame_size', [(3840, 1080), (1920, 540)])
def test_left_eye_jpeg_has_the_calibrated_size(frame_size):
    data = left_eye_jpeg(_sbs_jpeg(*frame_size), (1920, 1080))
    eye = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_COLOR)
    assert eye.shape == (1080, 1920, 3)
    assert abs(float(eye[:, :-32].mean()) - 50) < 3


def test_left_eye_jpeg_rejects_bad_input():
    assert left_eye_jpeg(b'not a jpeg', (1920, 1080)) is None
