#!/usr/bin/env python3

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

"""Bring up the robot_localization EKF for the Cobra Flex.

The EKF fuses the wheel odometry velocities (``odom/wheel``) into an
``odom -> base_link`` transform plus ``/odom`` (remapped from
``odometry/filtered``). Run ``cobra_flex_control`` wheel_odometry with
``publish_tf:=false`` so the EKF is the sole owner of the transform (the
bringup launch wires this automatically).
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PythonExpression
from launch_ros.actions import Node


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory('cobra_flex_localization'), 'config', 'ekf.yaml')

    return LaunchDescription([
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('ekf_params_file', default_value=default_params,
                              description='robot_localization params file.'),
        Node(
            package='robot_localization',
            executable='ekf_node',
            name='ekf_filter_node',
            output='screen',
            parameters=[
                LaunchConfiguration('ekf_params_file'),
                {'use_sim_time': PythonExpression(
                    ['"', LaunchConfiguration('use_sim_time'), '" in ("true", "1")'])},
            ],
            # The raw wheel estimate stays on odom/wheel; the fused estimate
            # becomes the canonical /odom.
            remappings=[('odometry/filtered', '/odom')],
        ),
    ])
