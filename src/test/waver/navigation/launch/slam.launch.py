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

"""Bring up slam_toolbox with its TF frames prefixed for one robot.

slam_toolbox reads map_frame / odom_frame / base_frame from a params yaml, not
from launch arguments, so there is no clean way to prefix them from XML. This
helper rewrites those three keys to the ``<robot_name>/`` prefix (matching the
frame_prefix used by robot_state_publisher and the other nodes in
waver.launch.xml) and forwards the result to slam_toolbox's stock
online_async launch. The scan_topic and all tuning parameters are inherited
unchanged from the source params file.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from nav2_common.launch import RewrittenYaml


def _launch_setup(context, *args, **kwargs):
    robot_name = LaunchConfiguration('robot_name').perform(context)
    use_sim_time = LaunchConfiguration('use_sim_time').perform(context)
    slam_params_file = LaunchConfiguration('slam_params_file').perform(context)

    # Empty robot_name -> no prefix (single-robot behaviour preserved).
    prefix = f'{robot_name}/' if robot_name else ''

    rewritten_params = RewrittenYaml(
        source_file=slam_params_file,
        param_rewrites={
            'map_frame': f'{prefix}map',
            'odom_frame': f'{prefix}odom',
            'base_frame': f'{prefix}base_footprint',
        },
        convert_types=True,
    )

    slam_launch = os.path.join(
        get_package_share_directory('slam_toolbox'),
        'launch',
        'online_async_launch.py',
    )

    return [
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(slam_launch),
            launch_arguments={
                'use_sim_time': use_sim_time,
                'slam_params_file': rewritten_params,
            }.items(),
        )
    ]


def generate_launch_description():
    # Pi-4-tuned slam params owned by this package (NOT the vendored waver_nav copy,
    # which vcs re-imports would wipe). Lightened so slam_toolbox keeps map->odom
    # under real-time on the CPU-bound Pi -- see config/slam_params.yaml header.
    default_params = os.path.join(
        get_package_share_directory('waver_navigation'),
        'config',
        'slam_params.yaml',
    )

    return LaunchDescription([
        DeclareLaunchArgument('robot_name', default_value='robot_1',
                              description='TF frame prefix for slam_toolbox map/odom/base frames.'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('slam_params_file', default_value=default_params,
                              description='Source slam_toolbox params; frames are rewritten per robot_name.'),
        OpaqueFunction(function=_launch_setup),
    ])
