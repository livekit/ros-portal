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

from datetime import datetime
from datetime import timezone
from pathlib import Path
import re

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.actions import IncludeLaunchDescription
from launch.actions import LogInfo
from launch.actions import OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


_UNSAFE_BAG_NAME_CHARACTERS = re.compile(r'[^A-Za-z0-9_.-]+')


def _sanitize_room_name(room_name: str) -> str:
    room_name = room_name.strip()
    if not room_name:
        raise RuntimeError('room_name launch argument must be non-empty')

    bag_name = _UNSAFE_BAG_NAME_CHARACTERS.sub('_', room_name).strip('._-')
    if not bag_name:
        raise RuntimeError('room_name must contain a letter or number')
    return bag_name


def _make_bag_output_path(
    room_name: str,
    bag_output_dir: str,
    timestamp: datetime | None = None,
) -> Path:
    bag_name = _sanitize_room_name(room_name)
    bag_output_dir = bag_output_dir.strip()
    if not bag_output_dir:
        raise RuntimeError('bag_output_dir launch argument must be non-empty')

    output_dir = Path(bag_output_dir).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    timestamp = timestamp or datetime.now(timezone.utc)
    timestamp_text = timestamp.astimezone(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    return output_dir / f'{bag_name}_{timestamp_text}'


def _launch_setup(context, *args, **kwargs):
    room_name = LaunchConfiguration('room_name').perform(context)
    bag_output_path = _make_bag_output_path(
        room_name,
        LaunchConfiguration('bag_output_dir').perform(context),
    )

    ros_portal_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([
                FindPackageShare('ros_portal'),
                'launch',
                'ros_portal_local.launch.py',
            ])
        ),
        launch_arguments={
            'config_path': PathJoinSubstitution([
                FindPackageShare('ros_portal_tutorials'),
                'config',
                'turtle_sim_bagger.yaml',
            ]),
            'livekit_url': LaunchConfiguration('livekit_url'),
            'identity': LaunchConfiguration('identity'),
            'room_name': room_name,
            'token': LaunchConfiguration('token'),
            'token_valid_for': LaunchConfiguration('token_valid_for'),
            'use_dev_credentials': LaunchConfiguration('use_dev_credentials'),
        }.items(),
    )

    bag_recorder = ExecuteProcess(
        cmd=['ros2', 'bag', 'record', '-a', '-o', str(bag_output_path)],
        output='screen',
        emulate_tty=True,
    )

    return [
        ros_portal_launch,
        LogInfo(msg=f'Recording ROS topics to {bag_output_path}'),
        bag_recorder,
    ]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('room_name', default_value='turtle_room'),
        DeclareLaunchArgument('identity', default_value='bagger'),
        DeclareLaunchArgument('bag_output_dir', default_value='./bags'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('token', default_value=''),
        DeclareLaunchArgument('token_valid_for', default_value='1h'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        OpaqueFunction(function=_launch_setup),
    ])
