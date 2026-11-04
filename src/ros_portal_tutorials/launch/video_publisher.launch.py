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

# Video pass-through tutorial, publisher side. See docs/tutorials.md.
#
# Plays the camera topic from a bag and runs a ROS Portal node that sends it to
# the LiveKit room as a video track.

from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.actions import SetEnvironmentVariable
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare

CAMERA_TOPIC = '/insta/cam0/image_raw/compressed'


def _as_bool(value: str) -> bool:
    return value.strip().lower() in ('1', 'true', 'yes', 'on')


def _bag_play(context, *args, **kwargs):
    bag_path = LaunchConfiguration('bag_path').perform(context).strip()
    if not bag_path:
        raise RuntimeError(
            'Set bag_path:=<bag directory>, for example bag_path:=/livekit_ws/data/r01_bag')
    bag = Path(bag_path).expanduser()
    if not bag.exists():
        raise RuntimeError(f'Bag does not exist: {bag}')

    cmd = [
        'ros2', 'bag', 'play', str(bag),
        '--topics', CAMERA_TOPIC,
        '--rate', LaunchConfiguration('rate').perform(context),
    ]
    if _as_bool(LaunchConfiguration('loop').perform(context)):
        cmd.append('--loop')
    return [ExecuteProcess(cmd=cmd, output='screen')]


def generate_launch_description():
    ros_portal_launch = PathJoinSubstitution([
        FindPackageShare('ros_portal'),
        'launch',
        'ros_portal_local.launch.py',
    ])
    config = PathJoinSubstitution([
        FindPackageShare('ros_portal_tutorials'),
        'config',
        'video_publisher.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument(
            'bag_path',
            default_value='',
            description='Bag directory that contains ' + CAMERA_TOPIC + '.',
        ),
        DeclareLaunchArgument(
            'domain_id',
            default_value='42',
            description='ROS_DOMAIN_ID for this side. Must differ from the subscriber side.',
        ),
        DeclareLaunchArgument('room_name', default_value='video_room'),
        DeclareLaunchArgument('identity', default_value='video_publisher'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument(
            'loop', default_value='true', description='Restart the bag at its end.'),
        DeclareLaunchArgument('rate', default_value='1.0', description='Bag playback rate.'),
        SetEnvironmentVariable('ROS_DOMAIN_ID', LaunchConfiguration('domain_id')),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(ros_portal_launch),
            launch_arguments={
                'config_path': config,
                'identity': LaunchConfiguration('identity'),
                'room_name': LaunchConfiguration('room_name'),
                'livekit_url': LaunchConfiguration('livekit_url'),
            }.items(),
        ),
        OpaqueFunction(function=_bag_play),
    ])
