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

"""Bring up the Cobra Flex physical stack: driver, wheel odometry, optional EKF.

Nodes:
  * cobra_flex_driver -- serial bridge: cmd_vel in; wheel_states/battery_state out.
  * wheel_odometry    -- integrates wheel_states into odom/wheel (+ TF unless the
                         EKF owns it).
  * ekf_filter_node   -- (use_ekf:=true) robot_localization smoothing over the
                         wheel odometry, republished as /odom. Mostly a
                         placeholder until an IMU or laser odometry is added.

The chassis has no additional sensors today, so this is the whole stack.
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


def _launch_setup(context, *args, **kwargs):
    params_file = LaunchConfiguration('params_file').perform(context)
    serial_port = LaunchConfiguration('serial_port').perform(context)
    use_ekf = LaunchConfiguration('use_ekf').perform(context).lower() in ('true', '1')

    driver_overrides = {}
    if serial_port:
        driver_overrides['serial_port'] = serial_port

    actions = [
        Node(
            package='cobra_flex_driver',
            executable='cobra_flex_driver',
            name='cobra_flex_driver',
            output='screen',
            parameters=[params_file, driver_overrides],
        ),
        Node(
            package='cobra_flex_control',
            executable='wheel_odometry',
            name='wheel_odometry',
            output='screen',
            # With the EKF enabled it owns odom -> base_link; the integrator
            # then only publishes the odom/wheel topic the EKF consumes.
            parameters=[params_file, {'publish_tf': not use_ekf}],
        ),
    ]
    if use_ekf:
        actions.append(IncludeLaunchDescription(
            PythonLaunchDescriptionSource(os.path.join(
                get_package_share_directory('cobra_flex_localization'),
                'launch', 'ekf.launch.py')),
            launch_arguments={
                'use_sim_time': LaunchConfiguration('use_sim_time'),
            }.items(),
        ))
    return actions


def generate_launch_description():
    default_params = os.path.join(
        get_package_share_directory('cobra_flex_bringup'), 'config', 'cobra_flex.yaml')

    return LaunchDescription([
        DeclareLaunchArgument('params_file', default_value=default_params,
                              description='Parameters for the driver and wheel odometry.'),
        DeclareLaunchArgument('serial_port', default_value='',
                              description='Override the driver serial port; empty keeps '
                                          'the params_file value.'),
        DeclareLaunchArgument('use_ekf', default_value='false',
                              description='true -> run the robot_localization EKF '
                                          '(cobra_flex_localization) and let it own the '
                                          'odom -> base_link transform.'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        OpaqueFunction(function=_launch_setup),
    ])
