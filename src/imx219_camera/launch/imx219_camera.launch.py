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
"""Bring up the Arducam IMX219 as sensor_msgs/msg/Image via camera_ros.

Prerequisite: libcamera enumerates cameras through udev. In a container that
starts without a udev daemon, run this once per boot first:

    ros2 run imx219_camera setup_camera_udev.sh   # (needs root)

Then:

    ros2 launch imx219_camera imx219_camera.launch.py
"""

import os

from ament_index_python.packages import get_package_prefix, get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    # The in-process IPA interposer built by this package. LD_PRELOAD'ing it into
    # the camera process makes libcamera load the Raspberry Pi IPA in-process
    # instead of in a crashing isolated worker (see src/libcamera_ipa_inprocess.cpp).
    ipa_shim = os.path.join(
        get_package_prefix('imx219_camera'),
        'lib', 'imx219_camera', 'libcamera_ipa_inprocess.so',
    )

    # Optional advanced libcamera controls (Ae/Awb/etc.) live here.
    config = os.path.join(
        get_package_share_directory('imx219_camera'), 'config', 'imx219_camera.yaml')

    args = [
        DeclareLaunchArgument('camera', default_value='0',
                              description='libcamera camera index (or id string)'),
        DeclareLaunchArgument('format', default_value='RGB888',
                              description='libcamera pixel format; RGB888 -> ROS bgr8'),
        DeclareLaunchArgument('width', default_value='1280'),
        DeclareLaunchArgument('height', default_value='720'),
        DeclareLaunchArgument('frame_id', default_value='camera'),
        DeclareLaunchArgument('namespace', default_value=''),
        DeclareLaunchArgument('camera_info_url', default_value='',
                              description='file:// URL to a calibration yaml (optional)'),
    ]

    camera_node = Node(
        package='camera_ros',
        executable='camera_node',
        name='camera',
        namespace=LaunchConfiguration('namespace'),
        output='screen',
        additional_env={'LD_PRELOAD': ipa_shim},
        parameters=[
            config,
            {
                'camera': ParameterValue(LaunchConfiguration('camera'), value_type=int),
                'format': LaunchConfiguration('format'),
                'width': ParameterValue(LaunchConfiguration('width'), value_type=int),
                'height': ParameterValue(LaunchConfiguration('height'), value_type=int),
                'frame_id': LaunchConfiguration('frame_id'),
                'camera_info_url': LaunchConfiguration('camera_info_url'),
            },
        ],
    )

    return LaunchDescription(args + [camera_node])
