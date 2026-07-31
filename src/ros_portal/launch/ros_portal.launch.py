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

from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _launch_setup(context, *args, **kwargs):
    config_path_value = LaunchConfiguration('config_path').perform(context).strip()
    if not config_path_value:
        raise RuntimeError('The `config_path` launch argument is required.')

    config_path = Path(config_path_value).expanduser()
    if not config_path.is_file():
        raise RuntimeError(f'Config file does not exist: {config_path}')

    return [
        Node(
            package='ros_portal',
            executable='ros_portal_node',
            name='ros_portal',
            output='screen',
            parameters=[{'config_path': str(config_path)}],
        ),
    ]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'config_path',
            description='Path to ROS Portal config YAML file.',
        ),
        OpaqueFunction(function=_launch_setup),
    ])
