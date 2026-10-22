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

"""Decode and split side-by-side (SBS) stereo JPEG frames."""

import cv2
import numpy as np

# libjpeg decodes at 1/2, 1/4, or 1/8 size in the DCT, which is faster than a
# full decode followed by a resize.
_GRAYSCALE_DECODE_FLAGS = {
    1: cv2.IMREAD_GRAYSCALE,
    2: cv2.IMREAD_REDUCED_GRAYSCALE_2,
    4: cv2.IMREAD_REDUCED_GRAYSCALE_4,
    8: cv2.IMREAD_REDUCED_GRAYSCALE_8,
}

# Relative tolerance for the aspect ratio check. One pixel of rounding on a
# 1080-pixel axis is about 0.1 percent.
_ASPECT_TOLERANCE = 0.005


def decode_grayscale(data, reduction=1):
    """
    Decode a JPEG to a grayscale image, reduced by 1, 2, 4, or 8.

    Returns None if the data does not decode.
    """
    if reduction not in _GRAYSCALE_DECODE_FLAGS:
        raise ValueError(f'reduction must be one of {sorted(_GRAYSCALE_DECODE_FLAGS)}')
    return cv2.imdecode(np.frombuffer(data, dtype=np.uint8), _GRAYSCALE_DECODE_FLAGS[reduction])


def frame_scale(frame_size, sbs_size):
    """
    Return the scale of a frame relative to the calibrated SBS size.

    Returns None if the aspect ratio differs, because then the frame is not a
    uniformly scaled copy of the calibrated mode.
    """
    width_scale = frame_size[0] / sbs_size[0]
    height_scale = frame_size[1] / sbs_size[1]
    if abs(width_scale - height_scale) > _ASPECT_TOLERANCE * height_scale:
        return None
    return height_scale


def split_sbs(frame):
    """Return the (left, right) halves of an SBS frame as views, without a copy."""
    half = frame.shape[1] // 2
    return frame[:, :half], frame[:, half:2 * half]


def left_eye_jpeg(data, eye_size, quality=95):
    """
    Return the left eye of an SBS JPEG as a new JPEG of eye_size (width, height).

    If the frame is a scaled copy of the calibrated mode, the eye is resized to
    eye_size, so that the calibrated intrinsics apply. Returns None if the data
    does not decode.
    """
    frame = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        return None
    left, _ = split_sbs(frame)
    if (left.shape[1], left.shape[0]) != tuple(eye_size):
        left = cv2.resize(left, tuple(eye_size), interpolation=cv2.INTER_LINEAR)
    ok, encoded = cv2.imencode('.jpg', left, [cv2.IMWRITE_JPEG_QUALITY, quality])
    return encoded.tobytes() if ok else None
