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
from launch.actions import SetEnvironmentVariable
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from launch_ros.substitutions import FindPackageShare


def _launch_setup(context, *args, **kwargs):
    config_path_value = LaunchConfiguration('config_path').perform(context).strip()
    if config_path_value:
        config_path = Path(config_path_value).expanduser()
        if not config_path.is_file():
            raise RuntimeError(f'Config file does not exist: {config_path}')
        config_path_value = str(config_path)

    container_name = LaunchConfiguration('container_name').perform(context).strip()
    if not container_name:
        raise RuntimeError('container_name launch argument must be non-empty')

    container_threads_value = LaunchConfiguration('container_threads').perform(context).strip()
    try:
        container_threads = int(container_threads_value)
    except ValueError as exc:
        raise RuntimeError('container_threads must be an integer') from exc
    if container_threads < 0:
        raise RuntimeError('container_threads must be non-negative')

    return [
        ComposableNodeContainer(
            name=container_name,
            namespace='',
            package='rclcpp_components',
            executable='component_container_mt',
            output='screen',
            emulate_tty=True,
            parameters=[{'thread_num': container_threads}],
            composable_node_descriptions=[
                ComposableNode(
                    package='ros_portal',
                    plugin='ros_portal::RosPortalComponent',
                    name='ros_portal',
                    namespace=LaunchConfiguration('ns').perform(context),
                    parameters=[{'config_path': config_path_value}],
                ),
            ],
        ),
    ]


def generate_launch_description():
    default_config = PathJoinSubstitution([
        FindPackageShare('ros_portal'),
        'config',
        'all_topics.yaml',
    ])

    return LaunchDescription([
        SetEnvironmentVariable('RCUTILS_COLORIZED_OUTPUT', '1'),
        DeclareLaunchArgument('config_path', default_value=default_config),
        DeclareLaunchArgument('container_name', default_value='ros_portal_container'),
        DeclareLaunchArgument(
            'container_threads',
            default_value='0',
            description='Component container worker threads. Use 0 for the rclcpp default.',
        ),
        DeclareLaunchArgument('ns', default_value='/'),
        OpaqueFunction(function=_launch_setup),
    ])
