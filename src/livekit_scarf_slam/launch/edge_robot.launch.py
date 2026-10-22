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

# ScaRF-SLAM demo, edge robot side (Jetson). See README.md.
#
# Starts ROS Portal, which sends /stereo/image_raw/compressed as the LiveKit
# video track /stereo/image_raw. Start the camera driver separately.

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    ros_portal_launch = PathJoinSubstitution([
        FindPackageShare('ros_portal'), 'launch', 'ros_portal_local.launch.py'])
    config = PathJoinSubstitution([
        FindPackageShare('livekit_scarf_slam'), 'config', 'ros_portal', 'edge_robot.yaml'])

    return LaunchDescription([
        DeclareLaunchArgument('room_name', default_value='robo_room'),
        DeclareLaunchArgument('identity', default_value='edge_robot'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(ros_portal_launch),
            launch_arguments={
                'config_path': config,
                'identity': LaunchConfiguration('identity'),
                'room_name': LaunchConfiguration('room_name'),
                'livekit_url': LaunchConfiguration('livekit_url'),
            }.items(),
        ),
    ])
