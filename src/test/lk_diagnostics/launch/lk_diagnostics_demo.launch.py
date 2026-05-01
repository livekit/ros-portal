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

"""Launch the simulated LiveKit diagnostics publisher and aggregator."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    """Generate the diagnostics PoC launch description."""
    package_share = get_package_share_directory('lk_diagnostics')
    aggregator_config = os.path.join(
        package_share,
        'config',
        'lk_diagnostics_aggregator.yaml',
    )

    diagnostic_period = LaunchConfiguration('diagnostic_period_sec')
    scenario_period = LaunchConfiguration('scenario_period_sec')

    return LaunchDescription([
        DeclareLaunchArgument(
            'diagnostic_period_sec',
            default_value='1.0',
            description='Seconds between diagnostic publications.',
        ),
        DeclareLaunchArgument(
            'scenario_period_sec',
            default_value='5.0',
            description='Seconds before the simulated diagnostic state changes.',
        ),
        Node(
            package='lk_diagnostics',
            executable='lk_diagnostics_node.py',
            name='lk_diagnostics',
            output='screen',
            parameters=[{
                'diagnostic_updater.period': ParameterValue(
                    diagnostic_period,
                    value_type=float,
                ),
                'scenario_period_sec': ParameterValue(
                    scenario_period,
                    value_type=float,
                ),
            }],
        ),
        Node(
            package='diagnostic_aggregator',
            executable='aggregator_node',
            name='diagnostic_aggregator',
            output='screen',
            parameters=[aggregator_config],
        ),
    ])
