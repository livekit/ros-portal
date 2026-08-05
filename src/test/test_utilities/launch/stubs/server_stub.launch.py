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

"""Launch minimal std_srvs stub servers for manual ROS Portal testing."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.actions import OpaqueFunction
from launch.actions import SetEnvironmentVariable
from launch.substitutions import LaunchConfiguration

_STUB_SCRIPT = """
import os

import rclpy
from rclpy.node import Node
from std_srvs.srv import SetBool, Trigger


class SetBoolStub(Node):
    def __init__(self, set_service_name, get_service_name):
        super().__init__('set_bool_stub')
        self._state = False
        self.create_service(SetBool, set_service_name, self.on_set_bool)
        self.create_service(Trigger, get_service_name, self.on_get_bool)

    def on_set_bool(self, request, response):
        self._state = request.data
        response.success = request.data
        response.message = 'enabled' if request.data else 'disabled'
        return response

    def on_get_bool(self, _request, response):
        response.success = True
        response.message = f'state is: {self._state}'
        return response


def main():
    set_service_name = os.environ.get('ROS2_LK_STUB_SERVICE_NAME', '/test/set_bool')
    get_service_name = os.environ.get('ROS2_LK_STUB_GET_SERVICE_NAME', '/test/get_bool')
    rclpy.init()
    node = SetBoolStub(set_service_name, get_service_name)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
"""


def _launch_setup(context, *args, **kwargs):
    service_name = LaunchConfiguration('service_name').perform(context)
    get_service_name = LaunchConfiguration('get_service_name').perform(context)
    return [
        ExecuteProcess(
            cmd=['python3', '-c', _STUB_SCRIPT],
            additional_env={
                'ROS2_LK_STUB_SERVICE_NAME': service_name,
                'ROS2_LK_STUB_GET_SERVICE_NAME': get_service_name,
            },
            output='screen',
            emulate_tty=True,
            name='set_bool_stub',
        ),
    ]


def generate_launch_description():
    return LaunchDescription([
        SetEnvironmentVariable('RCUTILS_COLORIZED_OUTPUT', '1'),
        DeclareLaunchArgument(
            'service_name',
            default_value='/test/set_bool',
            description='Absolute service name for the SetBool stub server.',
        ),
        DeclareLaunchArgument(
            'get_service_name',
            default_value='/test/get_bool',
            description='Absolute service name for the Trigger get-state stub server.',
        ),
        OpaqueFunction(function=_launch_setup),
    ])
