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

from composition_interfaces.srv import ListNodes, LoadNode, UnloadNode
from launch import LaunchDescription
from launch.actions import SetEnvironmentVariable, UnsetEnvironmentVariable
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
import launch_testing
import launch_testing.actions
import launch_testing.asserts
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import GetParameters
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
                plugin='ros_portal::RosPortalManager',
                name='ros_portal_component_a',
                parameters=[{'config_path': str(config_path), 'min_qos_depth': 5}],
            ),
            ComposableNode(
                package='ros_portal',
                plugin='ros_portal::RosPortalManager',
                name='ros_portal_component_b',
                parameters=[{'config_path': str(config_path), 'min_qos_depth': 5}],
            ),
        ],
    )

    return (
        LaunchDescription([
            SetEnvironmentVariable('LIVEKIT_URL', 'ws://127.0.0.1:1'),
            SetEnvironmentVariable('LIVEKIT_TOKEN', 'unused-component-test-token'),
            UnsetEnvironmentVariable('LIVEKIT_TOKEN_ENDPOINT'),
            UnsetEnvironmentVariable('LIVEKIT_TOKEN_SERVER_ID'),
            container,
            launch_testing.actions.ReadyToTest(),
        ]),
        {'container': container},
    )


class TestRosPortalComponent(unittest.TestCase):
    def _call(self, observer, service_type, service_name, request):
        client = observer.create_client(service_type, service_name)
        try:
            self.assertTrue(client.wait_for_service(timeout_sec=10.0), service_name)
            future = client.call_async(request)
            rclpy.spin_until_future_complete(observer, future, timeout_sec=10.0)
            self.assertTrue(future.done(), service_name)
            return future.result()
        finally:
            observer.destroy_client(client)

    def test_components_initialize_unload_and_reload(self):
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

            service_prefix = '/ros_portal_component_test_container/_container'

            def list_nodes():
                return self._call(
                    observer, ListNodes, f'{service_prefix}/list_nodes', ListNodes.Request()
                )

            def unload(node_id):
                response = self._call(
                    observer, UnloadNode, f'{service_prefix}/unload_node',
                    UnloadNode.Request(unique_id=node_id),
                )
                self.assertTrue(response.success, response.error_message)

            def load(name, invalid_config=False):
                request = LoadNode.Request(
                    package_name='ros_portal',
                    plugin_name='ros_portal::RosPortalManager',
                    node_name=name,
                )
                if invalid_config:
                    # A directory cannot be parsed as a config file. The
                    # constructor must reject this load and unwind its lease.
                    request.parameters = [Parameter(
                        name='config_path',
                        value=ParameterValue(
                            type=ParameterType.PARAMETER_STRING, string_value='/',
                        ),
                    )]
                return self._call(observer, LoadNode, f'{service_prefix}/load_node', request)

            loaded = list_nodes()
            ids = dict(zip(loaded.full_node_names, loaded.unique_ids))
            self.assertEqual(set(ids), {f'/{name}' for name in expected_names})

            failed = load('invalid_portal', invalid_config=True)
            self.assertFalse(failed.success)
            self.assertIn('Failed to initialize ROS Portal', failed.error_message)

            unload(ids['/ros_portal_component_a'])
            self.assertEqual(list(list_nodes().full_node_names), ['/ros_portal_component_b'])
            parameters = self._call(
                observer, GetParameters, '/ros_portal_component_b/get_parameters',
                GetParameters.Request(names=['min_qos_depth']),
            )
            self.assertEqual(parameters.values[0].type, ParameterType.PARAMETER_INTEGER)
            self.assertEqual(parameters.values[0].integer_value, 5)

            unload(ids['/ros_portal_component_b'])
            self.assertEqual(list(list_nodes().unique_ids), [])

            # Also exercise failure with no remaining runtime owner, followed
            # by a successful load that must initialize the SDK again.
            self.assertFalse(load('invalid_portal', invalid_config=True).success)
            reloaded = load('reloaded_portal')
            self.assertTrue(reloaded.success, reloaded.error_message)
            self.assertEqual(reloaded.full_node_name, '/reloaded_portal')
            unload(reloaded.unique_id)
            self.assertEqual(list(list_nodes().unique_ids), [])
        finally:
            observer.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestRosPortalComponentShutdown(unittest.TestCase):
    def test_container_exits_cleanly(self, proc_info, container):
        launch_testing.asserts.assertExitCodes(proc_info, process=container)
