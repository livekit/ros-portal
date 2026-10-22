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

"""
Fisheye stereo calibration from the OpenCV ChArUco calibration tool.

The input is stereo_calibration.json. cam0 is the left lens and cam1 is the
right lens. The OpenCV fisheye model is the Kalibr pinhole-equi model, which
OpenVINS and ScaRF-SLAM also use.
"""

from dataclasses import dataclass
from dataclasses import replace
import json
from pathlib import Path

import cv2
from livekit_scarf_slam.transforms import make_transform
import numpy as np


@dataclass(frozen=True)
class FisheyeCamera:
    """One lens: intrinsics, distortion, and its stereo rectification."""

    camera_matrix: np.ndarray
    """3x3 intrinsic matrix of the fisheye image."""
    distortion: np.ndarray
    """Equidistant distortion coefficients k1 to k4."""
    rect_rotation: np.ndarray
    """3x3 rotation from this camera frame to its rectified frame."""
    rect_projection: np.ndarray
    """3x4 projection matrix of the rectified image."""


@dataclass(frozen=True)
class StereoCalibration:
    """Calibration of a side-by-side (SBS) fisheye stereo camera."""

    image_size: tuple
    """(width, height) of one eye."""
    rect_size: tuple
    """(width, height) of the rectified images that the projections use."""
    left: FisheyeCamera
    right: FisheyeCamera
    rotation_left_to_right: np.ndarray
    translation_left_to_right: np.ndarray

    @property
    def sbs_size(self):
        """(width, height) of the SBS frame."""
        return (2 * self.image_size[0], self.image_size[1])

    @property
    def baseline_m(self):
        """Distance between the lenses in meters, from the rectified projections."""
        return -self.right.rect_projection[0, 3] / self.right.rect_projection[0, 0]

    @property
    def transform_cam1_cam0(self):
        """4x4 transform that maps points from the cam0 frame to the cam1 frame."""
        return make_transform(self.rotation_left_to_right, self.translation_left_to_right)

    def scaled(self, factor):
        """
        Return the calibration for input frames that are scaled by factor.

        The fisheye intrinsics scale with the input image. The rectified
        output (rect_size and the projections) does not change, so the output
        images stay the same when the input size changes.
        """
        def scale_camera(camera):
            camera_matrix = camera.camera_matrix.copy()
            camera_matrix[:2] *= factor
            return replace(camera, camera_matrix=camera_matrix)

        return replace(
            self,
            image_size=_scale_size(self.image_size, factor),
            left=scale_camera(self.left),
            right=scale_camera(self.right))


def _scale_size(size, factor):
    return (int(round(size[0] * factor)), int(round(size[1] * factor)))


def _camera_from_dict(name, camera, rect_rotation, rect_projection):
    camera_matrix = np.array(camera['camera_matrix'], dtype=float)
    if camera_matrix[0, 1] != 0.0:
        raise ValueError(f'{name}: camera matrix skew is not supported')
    distortion = np.array(camera['distortion'], dtype=float).reshape(-1)
    if distortion.size != 4:
        raise ValueError(f'{name}: expected 4 fisheye distortion values')
    return FisheyeCamera(
        camera_matrix=camera_matrix,
        distortion=distortion,
        rect_rotation=np.array(rect_rotation, dtype=float),
        rect_projection=np.array(rect_projection, dtype=float))


def calibration_from_dict(data):
    """Return the StereoCalibration of a parsed stereo_calibration.json."""
    if data.get('model') != 'opencv_fisheye':
        raise ValueError(f'unsupported model: {data.get("model")}')
    if not data.get('left_first', True):
        raise ValueError('left_first is false: the left lens must be the left half of the frame')
    rect = data['rectification_validation']
    if rect.get('projection') != 'pinhole':
        raise ValueError(f'unsupported rectified projection: {rect.get("projection")}')
    image_size = tuple(data['image_size'])
    source_size = tuple(data.get('source_stereo_size', (2 * image_size[0], image_size[1])))
    if source_size != (2 * image_size[0], image_size[1]):
        raise ValueError(f'source_stereo_size {source_size} is not two eyes of {image_size}')
    stereo = data['stereo']
    return StereoCalibration(
        image_size=image_size,
        rect_size=tuple(rect['output_size']),
        left=_camera_from_dict(
            'left', data['left'], rect['left_rotation'], rect['left_projection']),
        right=_camera_from_dict(
            'right', data['right'], rect['right_rotation'], rect['right_projection']),
        rotation_left_to_right=np.array(stereo['rotation_left_to_right'], dtype=float),
        translation_left_to_right=np.array(
            stereo['translation_left_to_right_m'], dtype=float).reshape(3))


def load_stereo_calibration(path):
    """Load stereo_calibration.json."""
    with Path(path).open(encoding='utf-8') as f:
        return calibration_from_dict(json.load(f))


def rectified_projection(camera, rect_scale):
    """Return the 3x4 rectified projection for output images scaled by rect_scale."""
    projection = camera.rect_projection.copy()
    projection[:2] *= rect_scale
    return projection


def rectify_maps(calibration, rect_scale):
    """
    Return the remap tables and the output size for both eyes.

    calibration must match the input frame size, for example after scaled().
    rect_scale is relative to calibration.rect_size, so pass the same absolute
    scale for every input size to keep the output images the same.

    Returns ((left_map1, left_map2), (right_map1, right_map2), (width, height)).
    """
    size = _scale_size(calibration.rect_size, rect_scale)

    def maps(camera):
        return cv2.fisheye.initUndistortRectifyMap(
            camera.camera_matrix, camera.distortion, camera.rect_rotation,
            rectified_projection(camera, rect_scale)[:, :3], size, cv2.CV_16SC2)

    return maps(calibration.left), maps(calibration.right), size


def kalibr_camchain_text(calibration, cam0_topic, cam1_topic):
    """
    Return a Kalibr camchain YAML text, the --cam input of kalibr_calibrate_imu_camera.

    T_cn_cnm1 of cam1 is the stereo transform from the cam0 frame to the cam1 frame.
    """
    resolution = list(calibration.image_size)
    lines = ['# Generated by convert_stereo_calibration.py. Do not edit.']
    for name, camera, topic, overlaps in (
            ('cam0', calibration.left, cam0_topic, [1]),
            ('cam1', calibration.right, cam1_topic, [0])):
        lines += [
            f'{name}:',
            f'  cam_overlaps: {overlaps}',
            '  camera_model: pinhole',
            f'  distortion_coeffs: {_floats(camera.distortion)}',
            '  distortion_model: equidistant',
            f'  intrinsics: {_floats(_intrinsics(camera))}',
            f'  resolution: {resolution}',
            f'  rostopic: {topic}',
        ]
    lines.append('  T_cn_cnm1:')
    lines += [f'  - {_floats(row)}' for row in calibration.transform_cam1_cam0]
    return '\n'.join(lines) + '\n'


def scarf_camera_blocks(calibration):
    """
    Return the fisheye_cam0 and fisheye_cam1 blocks of a ScaRF-SLAM config.

    ScaRF-SLAM uses each trajectory pose as the cam0 pose, and it reads
    T_cam_imu only to find the cam0 to cam1 offset. Without an IMU, the pose
    frame is cam0, so T_cam_imu is identity for cam0 and T_cam1_cam0 for cam1.
    """
    def block(camera, transform):
        return {
            'T_cam_imu': [_floats(row) for row in transform],
            'cam_overlaps': [],
            'camera_model': 'pinhole',
            'distortion_coeffs': _floats(camera.distortion),
            'distortion_model': 'equidistant',
            'intrinsics': _floats(_intrinsics(camera)),
            'resolution': list(calibration.image_size),
        }

    return {
        'fisheye_cam0': block(calibration.left, np.eye(4)),
        'fisheye_cam1': block(calibration.right, calibration.transform_cam1_cam0),
    }


def _intrinsics(camera):
    k = camera.camera_matrix
    return [k[0, 0], k[1, 1], k[0, 2], k[1, 2]]


def _floats(values):
    return [float(v) for v in values]
