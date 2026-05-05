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

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    package_share = get_package_share_directory('livekit_generate_params')
    params_file = os.path.join(package_share, 'config', 'livekit_generate_params.yaml')

    return LaunchDescription(
        [
            Node(
                package='livekit_generate_params',
                executable='livekit_generate_params_node',
                name='livekit_generate_params_node',
                parameters=[params_file],
                output='screen',
            )
        ]
    )
