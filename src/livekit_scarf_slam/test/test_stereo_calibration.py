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

import copy
import json
import os
from pathlib import Path
import subprocess
import sys

import cv2
from livekit_scarf_slam.stereo_calibration import calibration_from_dict
from livekit_scarf_slam.stereo_calibration import kalibr_camchain_text
from livekit_scarf_slam.stereo_calibration import load_stereo_calibration
from livekit_scarf_slam.stereo_calibration import rectified_projection
from livekit_scarf_slam.stereo_calibration import rectify_maps
from livekit_scarf_slam.stereo_calibration import scarf_camera_blocks
import numpy as np
import pytest
import yaml

PACKAGE_DIR = Path(__file__).resolve().parents[1]
CALIBRATION_PATH = PACKAGE_DIR / 'test' / 'data' / 'stereo_calibration.json'


@pytest.fixture(scope='module')
def raw():
    return json.loads(CALIBRATION_PATH.read_text(encoding='utf-8'))


@pytest.fixture(scope='module')
def calibration():
    return load_stereo_calibration(CALIBRATION_PATH)


def test_load(calibration, raw):
    assert calibration.image_size == (1920, 1080)
    assert calibration.sbs_size == (3840, 1080)
    assert calibration.rect_size == (1920, 1080)
    assert calibration.baseline_m == pytest.approx(raw['stereo']['baseline_m'], abs=2e-4)
    np.testing.assert_allclose(
        calibration.transform_cam1_cam0[:3, 3],
        np.array(raw['stereo']['translation_left_to_right_m']).reshape(3))


@pytest.mark.parametrize('change, message', [
    (lambda d: d.update(model='opencv_pinhole'), 'unsupported model'),
    (lambda d: d.update(left_first=False), 'left_first'),
    (lambda d: d['left']['camera_matrix'][0].__setitem__(1, 0.5), 'skew'),
    (lambda d: d.update(source_stereo_size=[1920, 1080]), 'source_stereo_size'),
])
def test_rejects_unsupported_input(raw, change, message):
    data = copy.deepcopy(raw)
    change(data)
    with pytest.raises(ValueError, match=message):
        calibration_from_dict(data)


def test_scaled(calibration):
    half = calibration.scaled(0.5)
    assert half.image_size == (960, 540)
    assert half.sbs_size == (1920, 540)
    assert half.rect_size == calibration.rect_size
    np.testing.assert_array_equal(half.left.rect_projection, calibration.left.rect_projection)
    assert half.left.camera_matrix[0, 0] == pytest.approx(calibration.left.camera_matrix[0, 0] / 2)
    assert half.left.camera_matrix[2, 2] == 1.0
    assert half.baseline_m == pytest.approx(calibration.baseline_m)
    np.testing.assert_array_equal(half.left.distortion, calibration.left.distortion)


@pytest.mark.parametrize('input_scale', [1.0, 0.5])
@pytest.mark.parametrize('pixel', [(480, 270), (100, 60), (850, 500)])
@pytest.mark.parametrize('depth', [1.0, 4.0])
def test_rectify_maps_match_the_fisheye_projection(calibration, input_scale, pixel, depth):
    # A rectified pixel must sample the fisheye pixel where the same 3D point
    # appears. The right eye checks that the stereo transform matches R2 and P2.
    rect_scale = 0.5
    scaled = calibration.scaled(input_scale)
    left_maps, right_maps, size = rectify_maps(scaled, rect_scale)
    assert size == (960, 540)
    u, v = pixel

    for camera, maps in ((scaled.left, left_maps), (scaled.right, right_maps)):
        projection = rectified_projection(camera, rect_scale)
        fx, fy, cx, cy = projection[0, 0], projection[1, 1], projection[0, 2], projection[1, 2]
        # Back-project the pixel to a point in the rectified left frame. The
        # right projection holds Tx = -fx * baseline.
        point_rect = np.array([
            (u - cx) * depth / fx - projection[0, 3] / fx, (v - cy) * depth / fy, depth])
        point_left = scaled.left.rect_rotation.T @ point_rect
        point = point_left
        if camera is scaled.right:
            point = calibration.rotation_left_to_right @ point_left \
                + calibration.translation_left_to_right
        expected, _ = cv2.fisheye.projectPoints(
            point.reshape(1, 1, 3), np.zeros(3), np.zeros(3),
            camera.camera_matrix, camera.distortion)
        map_x, map_y = cv2.convertMaps(maps[0], maps[1], cv2.CV_32FC1)
        sampled = np.array([map_x[v, u], map_y[v, u]])
        np.testing.assert_allclose(sampled, expected.reshape(2), atol=0.5 * input_scale)


def test_rectified_rows_match(calibration):
    left = rectified_projection(calibration.left, 0.5)
    right = rectified_projection(calibration.right, 0.5)
    np.testing.assert_allclose(left[1], right[1])
    assert right[0, 3] == pytest.approx(-left[0, 0] * calibration.baseline_m)


def test_kalibr_camchain(calibration):
    chain = yaml.safe_load(kalibr_camchain_text(calibration, '/a', '/b'))
    assert chain['cam0']['rostopic'] == '/a'
    assert chain['cam0']['distortion_model'] == 'equidistant'
    assert chain['cam0']['resolution'] == [1920, 1080]
    k = calibration.left.camera_matrix
    assert chain['cam0']['intrinsics'] == [k[0, 0], k[1, 1], k[0, 2], k[1, 2]]
    np.testing.assert_allclose(chain['cam1']['T_cn_cnm1'], calibration.transform_cam1_cam0)


def test_scarf_camera_blocks(calibration):
    blocks = scarf_camera_blocks(calibration)
    np.testing.assert_allclose(blocks['fisheye_cam0']['T_cam_imu'], np.eye(4))
    np.testing.assert_allclose(
        blocks['fisheye_cam1']['T_cam_imu'], calibration.transform_cam1_cam0)
    # ScaRF-SLAM derives T_cam0_cam1 = T_cam0_imu @ inv(T_cam1_imu).
    t_cam0_cam1 = np.array(blocks['fisheye_cam0']['T_cam_imu']) @ np.linalg.inv(
        blocks['fisheye_cam1']['T_cam_imu'])
    np.testing.assert_allclose(
        t_cam0_cam1 @ calibration.transform_cam1_cam0, np.eye(4), atol=1e-12)
    assert blocks['fisheye_cam1']['distortion_coeffs'] == list(calibration.right.distortion)


def test_converter_scarf_keeps_other_keys(tmp_path, calibration):
    template = tmp_path / 'config.yaml'
    template.write_text('model_name: da\npinhole_resolution: [800, 600]\n', encoding='utf-8')
    env = dict(os.environ, PYTHONPATH=os.pathsep.join(
        [str(PACKAGE_DIR), os.environ.get('PYTHONPATH', '')]))
    subprocess.run(
        [sys.executable, str(PACKAGE_DIR / 'scripts' / 'convert_stereo_calibration.py'),
         'scarf', str(CALIBRATION_PATH), str(template)],
        check=True, env=env, capture_output=True)
    config = yaml.safe_load(template.read_text(encoding='utf-8'))
    assert config['model_name'] == 'da'
    assert config['pinhole_resolution'] == [800, 600]
    assert config['fisheye_cam0'] == scarf_camera_blocks(calibration)['fisheye_cam0']
