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

"""
Bring up the WAVE ROVER stack alongside the LiveKit bridge.

This keeps the composition (and its dependency on waver_bringup) out of the
reusable ros2_livekit_bridge package.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.actions import TimerAction
from launch.launch_description_sources import AnyLaunchDescriptionSource
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def _as_bool(value: str) -> bool:
    return value.strip().lower() in ('1', 'true', 'yes', 'on')


def _launch_setup(context, *args, **kwargs):
    sim_enabled = _as_bool(LaunchConfiguration('sim').perform(context))

    waver_launch = PathJoinSubstitution([
        FindPackageShare('waver_bringup'),
        'launch',
        'waver.launch.xml',
    ])
    waver = IncludeLaunchDescription(
        AnyLaunchDescriptionSource(waver_launch),
        launch_arguments={
            'sim': LaunchConfiguration('sim'),
            'sim_gui': LaunchConfiguration('sim_gui'),
        }.items(),
    )

    bridge_launch = PathJoinSubstitution([
        FindPackageShare('ros2_livekit_bridge'),
        'launch',
        'livekit_bridge_local.launch.py',
    ])
    bridge = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(bridge_launch),
        launch_arguments={
            'config': LaunchConfiguration('config'),
            'livekit_url': LaunchConfiguration('livekit_url'),
            'identity': LaunchConfiguration('identity'),
            'room_name': LaunchConfiguration('room_name'),
            'token_valid_for': LaunchConfiguration('token_valid_for'),
            'use_dev_credentials': LaunchConfiguration('use_dev_credentials'),
            'ns': LaunchConfiguration('ns'),
        }.items(),
    )

    # When running against the simulator, give the sim stack time to come up
    # before the bridge starts forwarding.
    bridge_action = TimerAction(period=10.0, actions=[bridge]) if sim_enabled else bridge

    return [waver, bridge_action]


def generate_launch_description():
    default_config = PathJoinSubstitution([
        FindPackageShare('waver_bringup'),
        'config',
        'livekit.yaml',
    ])

    return LaunchDescription([
        # WAVE ROVER stack arguments (forwarded to waver.launch.xml).
        DeclareLaunchArgument(
            'sim',
            default_value='false',
            description='Launch the WAVE ROVER stack with sim:=true.',
        ),
        DeclareLaunchArgument(
            'sim_gui',
            default_value='false',
            description='Launch the Gazebo GUI client when sim is enabled.',
        ),
        # Bridge arguments (forwarded to livekit_bridge_local.launch.py).
        DeclareLaunchArgument('config', default_value=default_config),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('identity', default_value='ros2-livekit-bridge'),
        DeclareLaunchArgument('room_name', default_value='robo_room'),
        DeclareLaunchArgument('token_valid_for', default_value='1h'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        DeclareLaunchArgument('ns', default_value='/'),
        OpaqueFunction(function=_launch_setup),
    ])
