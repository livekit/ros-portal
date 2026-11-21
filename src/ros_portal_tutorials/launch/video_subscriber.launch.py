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

# Video pass-through tutorial, subscriber side. See docs/tutorials.md.
#
# Runs a ROS Portal node that republishes the LiveKit video track as
# sensor_msgs/CompressedImage, plus an optional foxglove_bridge to view it.
# ROS Portal pauses the track while no ROS node subscribes, so frames start to
# flow when Foxglove (or another subscriber) opens the image topic.

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    ros_portal_launch = PathJoinSubstitution([
        FindPackageShare('ros_portal'),
        'launch',
        'ros_portal_local.launch.py',
    ])
    config = PathJoinSubstitution([
        FindPackageShare('ros_portal_tutorials'),
        'config',
        'video_subscriber.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument(
            'domain_id',
            default_value='100',
            description='ROS_DOMAIN_ID for this side. Must differ from the publisher side.',
        ),
        DeclareLaunchArgument('room_name', default_value='video_room'),
        DeclareLaunchArgument('identity', default_value='video_subscriber'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument(
            'foxglove', default_value='true', description='Start foxglove_bridge.'),
        DeclareLaunchArgument('foxglove_port', default_value='8765'),
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
        Node(
            package='foxglove_bridge',
            executable='foxglove_bridge',
            name='foxglove_bridge',
            output='screen',
            condition=IfCondition(LaunchConfiguration('foxglove')),
            parameters=[{
                'port': ParameterValue(LaunchConfiguration('foxglove_port'), value_type=int),
            }],
        ),
    ])
