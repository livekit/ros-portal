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
import unittest

from composition_interfaces.srv import ListNodes
from composition_interfaces.srv import LoadNode
from composition_interfaces.srv import UnloadNode
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.actions import SetEnvironmentVariable
from launch.launch_description_sources import PythonLaunchDescriptionSource
import launch_testing
import launch_testing.actions
import launch_testing.asserts
import rclpy
from rcl_interfaces.msg import Parameter
from rcl_interfaces.msg import ParameterType
from rcl_interfaces.msg import ParameterValue
from rcl_interfaces.srv import GetParameters


CONTAINER_NAME = '/ros_portal_component_test_container'
NODE_NAMESPACE = '/composition_test'
CONFIG_PATH = Path(__file__).resolve().parents[2] / 'config' / 'all_topics.yaml'


def generate_test_description():
    launch_file = Path(__file__).resolve().parents[2] / 'launch' / 'ros_portal_composable.launch.py'
    return LaunchDescription([
        SetEnvironmentVariable('LIVEKIT_URL', 'ws://127.0.0.1:1'),
        SetEnvironmentVariable('LIVEKIT_TOKEN', 'unused-component-test-token'),
        SetEnvironmentVariable('LIVEKIT_TOKEN_ENDPOINT', ''),
        SetEnvironmentVariable('LIVEKIT_TOKEN_SERVER_ID', ''),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(launch_file)),
            launch_arguments={
                'config_path': str(CONFIG_PATH),
                'container_name': CONTAINER_NAME.lstrip('/'),
                'ns': NODE_NAMESPACE,
                # Exercise the bundled launch file's default worker count.
            }.items(),
        ),
        launch_testing.actions.ReadyToTest(),
    ])


class TestRosPortalComponent(unittest.TestCase):
    def test_load_failure_unload_survivor_and_reload(self, proc_output):
        rclpy.init()
        observer = rclpy.create_node('ros_portal_component_test_observer')

        def call(service_type, service_name, request):
            client = observer.create_client(service_type, service_name)
            try:
                self.assertTrue(client.wait_for_service(timeout_sec=10.0), service_name)
                future = client.call_async(request)
                rclpy.spin_until_future_complete(observer, future, timeout_sec=10.0)
                self.assertTrue(future.done(), f'Timed out calling {service_name}')
                self.assertIsNone(future.exception())
                return future.result()
            finally:
                observer.destroy_client(client)

        def list_nodes():
            return call(ListNodes, CONTAINER_NAME + '/_container/list_nodes', ListNodes.Request())

        def load(name, config_path):
            request = LoadNode.Request()
            request.package_name = 'ros_portal'
            request.plugin_name = 'ros_portal::RosPortal'
            request.node_name = name
            request.node_namespace = NODE_NAMESPACE
            request.parameters = [Parameter(
                name='config_path',
                value=ParameterValue(type=ParameterType.PARAMETER_STRING, string_value=str(config_path)),
            )]
            return call(LoadNode, CONTAINER_NAME + '/_container/load_node', request)

        def unload(node_id):
            request = UnloadNode.Request()
            request.unique_id = node_id
            response = call(UnloadNode, CONTAINER_NAME + '/_container/unload_node', request)
            self.assertTrue(response.success, response.error_message)

        try:
            proc_output.assertWaitFor('ROS Portal initialized', timeout=15)
            initial = list_nodes()
            self.assertEqual(list(initial.full_node_names), [NODE_NAMESPACE + '/ros_portal'])

            failed = load('invalid_portal', '/nonexistent/ros_portal_component_test.yaml')
            self.assertFalse(failed.success)
            self.assertIn('Failed to initialize ROS Portal', failed.error_message)
            self.assertEqual(list(list_nodes().unique_ids), list(initial.unique_ids))

            second = load('surviving_portal', CONFIG_PATH)
            self.assertTrue(second.success, second.error_message)
            unload(initial.unique_ids[0])
            self.assertEqual(list(list_nodes().unique_ids), [second.unique_id])
            request = GetParameters.Request()
            request.names = ['config_path']
            parameters = call(GetParameters, NODE_NAMESPACE + '/surviving_portal/get_parameters', request)
            self.assertEqual(parameters.values[0].string_value, str(CONFIG_PATH))

            unload(second.unique_id)
            self.assertEqual(list(list_nodes().unique_ids), [])
            reloaded = load('reloaded_portal', CONFIG_PATH)
            self.assertTrue(reloaded.success, reloaded.error_message)
            unload(reloaded.unique_id)
            self.assertEqual(list(list_nodes().unique_ids), [])
        finally:
            observer.destroy_node()
            rclpy.shutdown()


@launch_testing.post_shutdown_test()
class TestRosPortalComponentShutdown(unittest.TestCase):
    def test_container_exits_cleanly(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
