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

"""Launch a full end-to-end ros_portal latency measurement.

Runs a 'robot' bridge on ROS_DOMAIN_ID=1 and a 'controller' bridge on
ROS_DOMAIN_ID=2, both joining the same LiveKit room. Separate ROS domains force
the probe traffic across LiveKit instead of local ROS discovery. Both configs
enable measure_latency.

Unless run_probes:=false, it also launches the probes in the matching domain:
the publisher on the robot domain (sends /ros_portal/latency/timestamp)
and the subscriber on the controller domain (reads the republished
.../timestamp_rx and publishes rolling stats on
/ros_portal/latency/stats). Watch it live with:

  ROS_DOMAIN_ID=2 ros2 topic echo /ros_portal/latency/stats

Each bridge is reused verbatim from
ros_portal/launch/ros_portal_local.launch.py (token minting, env,
and node), wrapped in a scoped group that sets its ROS_DOMAIN_ID.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import GroupAction
from launch.actions import IncludeLaunchDescription
from launch.actions import SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def _bridge_group(domain_id, identity, config_arg, extra_nodes):
    bridge_launch = PathJoinSubstitution([
        FindPackageShare('ros_portal'),
        'launch',
        'ros_portal_local.launch.py',
    ])
    return GroupAction(
        [
            SetEnvironmentVariable('ROS_DOMAIN_ID', domain_id),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(bridge_launch),
                launch_arguments={
                    'config': LaunchConfiguration(config_arg),
                    'identity': identity,
                    'room_name': LaunchConfiguration('room_name'),
                    'livekit_url': LaunchConfiguration('livekit_url'),
                    'use_dev_credentials': LaunchConfiguration('use_dev_credentials'),
                }.items(),
            ),
            *extra_nodes,
        ],
        scoped=True,
    )


def generate_launch_description():
    default_robot_config = PathJoinSubstitution([
        FindPackageShare('waveshare_launch'),
        'config',
        'waveshare_livekit_robot.yaml',
    ])
    default_controller_config = PathJoinSubstitution([
        FindPackageShare('waveshare_launch'),
        'config',
        'waveshare_livekit_controller.yaml',
    ])

    run_probes = IfCondition(LaunchConfiguration('run_probes'))

    # Probe publisher on the robot domain (T0 end).
    probe_publisher = Node(
        package='test_utilities',
        executable='latency_probe_publisher.py',
        name='latency_probe_publisher',
        output='screen',
        condition=run_probes,
        parameters=[{
            'rate_hz': ParameterValue(LaunchConfiguration('probe_rate_hz'), value_type=float),
            'payload_size': ParameterValue(LaunchConfiguration('probe_payload_size'), value_type=int),
        }],
    )

    # Probe subscriber on the controller domain (T5 end + stats).
    probe_subscriber = Node(
        package='test_utilities',
        executable='latency_probe_subscriber.py',
        name='latency_probe_subscriber',
        output='screen',
        condition=run_probes,
    )

    return LaunchDescription([
        DeclareLaunchArgument('room_name', default_value='latency_room'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        DeclareLaunchArgument('robot_config', default_value=default_robot_config),
        DeclareLaunchArgument('controller_config', default_value=default_controller_config),
        DeclareLaunchArgument('run_probes', default_value='true',
                              description='Also launch the latency probe publisher/subscriber.'),
        DeclareLaunchArgument('probe_rate_hz', default_value='100.0'),
        DeclareLaunchArgument('probe_payload_size', default_value='0'),
        _bridge_group('1', 'robot', 'robot_config', [probe_publisher]),
        _bridge_group('2', 'controller', 'controller_config', [probe_subscriber]),
    ])
