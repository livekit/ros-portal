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

"""Bring up the nav2 navigation stack with its TF frames prefixed for one robot.

nav2's servers read their frames (global_frame, robot_base_frame, ...) from a
params yaml, and the stock params use unprefixed frames (map / odom / base_link).
This robot prefixes every frame with ``<robot_name>/`` (matching the frame_prefix
used by robot_state_publisher and the other nodes in waver.launch.xml), so
without rewriting, every nav2 TF lookup would fail and the stack would never
activate. This helper rewrites the frame keys to the ``<robot_name>/`` prefix and
forwards the result to nav2_bringup's stock navigation_launch.

Frames are rewritten by fully-qualified dotted path (not leaf key), because
``global_frame`` must resolve to ``<prefix>map`` in the global costmap / bt /
behaviors but to ``<prefix>odom`` in the local costmap — a flat leaf rewrite
(as slam.launch.py uses) cannot split one key name into two values.

Topics stay flat (/scan, /odom, /cmd_vel, /map) and are inherited unchanged.
navigation_launch does NOT start map_server or amcl; the map and map->odom
transform come from the always-on slam_toolbox (see waver.launch.xml).
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from nav2_common.launch import RewrittenYaml


def _launch_setup(context, *args, **kwargs):
    robot_name = LaunchConfiguration('robot_name').perform(context)
    use_sim_time = LaunchConfiguration('use_sim_time').perform(context)
    params_file = LaunchConfiguration('params_file').perform(context)
    use_composition = LaunchConfiguration('use_composition').perform(context)

    # Empty robot_name -> no prefix (single-robot behaviour preserved).
    prefix = f'{robot_name}/' if robot_name else ''

    map_frame = f'{prefix}map'
    odom_frame = f'{prefix}odom'
    base_link_frame = f'{prefix}base_link'
    base_footprint_frame = f'{prefix}base_footprint'

    # Rewrite by absolute path so global_frame can be map in one server and odom
    # in another. Only nodes started by navigation_launch.py are rewritten; amcl,
    # map_saver and loopback_simulator are not launched here so their (unprefixed)
    # frames are harmless.
    rewritten_params = RewrittenYaml(
        source_file=params_file,
        param_rewrites={
            'bt_navigator.ros__parameters.global_frame': map_frame,
            'bt_navigator.ros__parameters.robot_base_frame': base_link_frame,

            'local_costmap.local_costmap.ros__parameters.global_frame': odom_frame,
            'local_costmap.local_costmap.ros__parameters.robot_base_frame': base_link_frame,

            'global_costmap.global_costmap.ros__parameters.global_frame': map_frame,
            'global_costmap.global_costmap.ros__parameters.robot_base_frame': base_link_frame,

            'behavior_server.ros__parameters.local_frame': odom_frame,
            'behavior_server.ros__parameters.global_frame': map_frame,
            'behavior_server.ros__parameters.robot_base_frame': base_link_frame,

            'collision_monitor.ros__parameters.base_frame_id': base_footprint_frame,
            'collision_monitor.ros__parameters.odom_frame_id': odom_frame,

            'docking_server.ros__parameters.base_frame': base_link_frame,
            'docking_server.ros__parameters.fixed_frame': odom_frame,
        },
        convert_types=True,
    )

    navigation_launch = os.path.join(
        get_package_share_directory('nav2_bringup'),
        'launch',
        'navigation_launch.py',
    )

    composed = use_composition.lower() in ('true', '1')

    actions = []

    # navigation_launch.py with use_composition:=True only LoadComposableNodes into an
    # existing container (normally created by bringup_launch.py); it does NOT create one.
    # Run standalone, that LoadComposableNodes blocks forever waiting for the container,
    # so we must create nav2_container here ourselves. Its fully-qualified name must be
    # /nav2_container to match navigation_launch's target (namespace '' + container_name).
    if composed:
        actions.append(Node(
            name='nav2_container',
            package='rclcpp_components',
            executable='component_container_isolated',
            parameters=[rewritten_params],
            output='screen',
        ))

    actions.append(
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(navigation_launch),
            launch_arguments={
                'use_sim_time': use_sim_time,
                'params_file': rewritten_params,
                # Compose all nav2 servers into one process (nav2_container, created
                # above) instead of ~10 separate processes. Big CPU/RAM/DDS win on the
                # Pi 4, which is CPU bound running slam + rf2o + nav2 + the bridge.
                'use_composition': use_composition,
            }.items(),
        )
    )

    return actions


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory('waver_navigation'),
        'config',
        'nav2_params.yaml',
    )

    return LaunchDescription([
        DeclareLaunchArgument('robot_name', default_value='robot_1',
                              description='TF frame prefix for the nav2 servers/costmaps.'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('params_file', default_value=default_params,
                              description='Source nav2 params; frames are rewritten per robot_name.'),
        DeclareLaunchArgument('use_composition', default_value='True',
                              description='Run all nav2 servers in one component container (CPU/RAM win on the Pi).'),
        OpaqueFunction(function=_launch_setup),
    ])
