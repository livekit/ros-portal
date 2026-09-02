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
Trace the normal ROS -> LiveKit -> ROS data path on two isolated ROS domains.

The bridge-specific events are captured with selected ROS core events in one
LTTng CTF trace. The workload uses the standard ``ros2 topic pub`` and
``ros2 topic hz`` commands; no timestamp-aware ROS messages or measurement
nodes participate in the forwarding path.

After stopping the launch, pass the trace directory printed by the Trace action
to:

  ros2 run latency_tools analyze_latency_trace.py <trace-directory>
"""

import json

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.actions import GroupAction
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.actions import SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from tracetools_launch.action import Trace


PROBE_TOPIC = '/ros_portal/latency/probe'
TRACE_EVENTS = [
    'ros2:rcl_init',
    'ros2:rcl_node_init',
    'ros2:rmw_publisher_init',
    'ros2:rcl_publisher_init',
    'ros2:rmw_subscription_init',
    'ros2:rcl_subscription_init',
    'ros2:rmw_publish',
    'ros2:rmw_take',
    'ros_portal:*',
]


def _bridge_group(domain_id, identity, config_arg, extra_actions):
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
            *extra_actions,
        ],
        scoped=True,
    )


def _probe_publisher(context):
    payload_size = int(LaunchConfiguration('probe_payload_size').perform(context))
    if payload_size < 0:
        raise ValueError('probe_payload_size must be non-negative')
    payload = json.dumps({'data': '0' * payload_size}, separators=(',', ':'))
    return [
        ExecuteProcess(
            cmd=[
                'ros2', 'topic', 'pub',
                '--rate', LaunchConfiguration('probe_rate_hz'),
                PROBE_TOPIC,
                'std_msgs/msg/String',
                payload,
            ],
            output='log'
        ),
    ]


def _probe_subscriber(_context):
    return [
        ExecuteProcess(
            cmd=[
                'ros2', 'topic', 'hz',
                '--window', '1000',
                PROBE_TOPIC,
            ],
            output='screen'
        ),
    ]


def generate_launch_description():
    default_robot_config = PathJoinSubstitution([
        FindPackageShare('latency_tools'),
        'config',
        'latency_robot.yaml',
    ])
    default_controller_config = PathJoinSubstitution([
        FindPackageShare('latency_tools'),
        'config',
        'latency_controller.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument('room_name', default_value='latency_room'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        DeclareLaunchArgument('robot_config', default_value=default_robot_config),
        DeclareLaunchArgument('controller_config', default_value=default_controller_config),
        DeclareLaunchArgument(
            'trace_session_name',
            default_value='ros_portal_latency',
        ),
        DeclareLaunchArgument('probe_rate_hz', default_value='100.0'),
        DeclareLaunchArgument('probe_payload_size', default_value='0'),
        Trace(
            session_name=LaunchConfiguration('trace_session_name'),
            append_timestamp=True,
            events_ust=TRACE_EVENTS,
            context_fields=['procname', 'vpid', 'vtid'],
            subbuffer_size_ust=8 * 1024 * 1024,
        ),
        _bridge_group('1', 'robot', 'robot_config', [OpaqueFunction(function=_probe_publisher)]),
        _bridge_group(
            '2', 'controller', 'controller_config',
            [OpaqueFunction(function=_probe_subscriber)],
        ),
    ])
