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

from pathlib import Path
import time
import unittest

from launch import LaunchDescription
from launch.actions import SetEnvironmentVariable
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
import rclpy


def generate_test_description():
    config_path = Path(__file__).resolve().parents[2] / 'config' / 'all_topics.yaml'
    container = ComposableNodeContainer(
        name='ros_portal_component_test_container',
        namespace='',
        package='rclcpp_components',
        executable='component_container_mt',
        output='screen',
        parameters=[{'thread_num': 1}],
        composable_node_descriptions=[
            ComposableNode(
                package='ros_portal',
                plugin='ros_portal::RosPortalComponent',
                name='ros_portal_component_a',
                parameters=[{'config_path': str(config_path)}],
            ),
            ComposableNode(
                package='ros_portal',
                plugin='ros_portal::RosPortalComponent',
                name='ros_portal_component_b',
                parameters=[{'config_path': str(config_path)}],
            ),
        ],
    )

    return (
        LaunchDescription([
            SetEnvironmentVariable('LIVEKIT_URL', 'ws://127.0.0.1:1'),
            SetEnvironmentVariable('LIVEKIT_TOKEN', 'unused-component-test-token'),
            container,
            launch_testing.actions.ReadyToTest(),
        ]),
        {'container': container},
    )


class TestRosPortalComponent(unittest.TestCase):
    def test_both_components_join_the_ros_graph(self):
        rclpy.init()
        observer = rclpy.create_node('ros_portal_component_test_observer')
        expected_names = {
            'ros_portal_component_a',
            'ros_portal_component_b',
        }

        try:
            deadline = time.monotonic() + 15.0
            discovered_names = set()
            while time.monotonic() < deadline:
                rclpy.spin_once(observer, timeout_sec=0.1)
                discovered_names = {name for name, _ in observer.get_node_names_and_namespaces()}
                if expected_names.issubset(discovered_names):
                    break

            self.assertTrue(
                expected_names.issubset(discovered_names),
                f'Expected component nodes {expected_names}, discovered {discovered_names}',
            )
        finally:
            observer.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestRosPortalComponentShutdown(unittest.TestCase):
    def test_container_exits_cleanly(self, proc_info, container):
        launch_testing.asserts.assertExitCodes(proc_info, process=container)
