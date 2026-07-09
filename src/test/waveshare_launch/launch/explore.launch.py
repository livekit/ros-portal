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

"""Bring up explore_lite frontier exploration with its base frame prefixed.

explore_lite roams autonomously: it finds frontiers (boundaries between mapped
and unknown space) on the slam_toolbox occupancy grid and sends each as a
NavigateToPose goal to nav2, until the map is fully explored (then returns to
the start pose).

Only robot_base_frame needs the "<robot_name>/" prefix: explore_lite reads its
global frame from the incoming /map message's frame_id (already <robot_name>/map
from slam_toolbox), and the NavigateToPose action + costmap topics stay flat.
This mirrors slam.launch.py / nav2.launch.py: rewrite the one frame key via
RewrittenYaml and launch the stock node.

Requires slam_toolbox (for /map) and the nav2 stack (for the navigate_to_pose
action) to be running — see waveshare.launch.xml, which gates this on nav2:=true.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from nav2_common.launch import RewrittenYaml


def _launch_setup(context, *args, **kwargs):
    robot_name = LaunchConfiguration('robot_name').perform(context)
    use_sim_time = LaunchConfiguration('use_sim_time').perform(context)
    params_file = LaunchConfiguration('params_file').perform(context)

    # Empty robot_name -> no prefix (single-robot behaviour preserved).
    prefix = f'{robot_name}/' if robot_name else ''

    rewritten_params = RewrittenYaml(
        source_file=params_file,
        param_rewrites={
            'explore_node.ros__parameters.robot_base_frame': f'{prefix}base_link',
        },
        convert_types=True,
    )

    return [
        Node(
            package='explore_lite',
            name='explore_node',
            executable='explore',
            parameters=[rewritten_params, {'use_sim_time': use_sim_time == 'true'}],
            output='screen',
        )
    ]


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory('waveshare_launch'),
        'config',
        'explore_params.yaml',
    )

    return LaunchDescription([
        DeclareLaunchArgument('robot_name', default_value='robot_1',
                              description='TF frame prefix for explore_lite robot_base_frame.'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('params_file', default_value=default_params,
                              description='Source explore_lite params; robot_base_frame is rewritten per robot_name.'),
        OpaqueFunction(function=_launch_setup),
    ])
