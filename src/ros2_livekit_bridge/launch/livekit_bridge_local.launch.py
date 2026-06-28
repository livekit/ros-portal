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
import re
import subprocess

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import OpaqueFunction
from launch.actions import SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import EqualsSubstitution
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def _as_bool(value: str) -> bool:
    return value.strip().lower() in ('1', 'true', 'yes', 'on')


def _room_name_from_config(config_path: Path) -> str:
    text = config_path.read_text(encoding='utf-8')
    match = re.search(r'^\s*room_name:\s*["\']?([^"\'\s#]+)', text, re.MULTILINE)
    if not match:
        raise RuntimeError(f'Could not find ros2_livekit_bridge.room_name in {config_path}')
    return match.group(1)


def _mint_token(room_name: str, identity: str, valid_for: str, use_dev_credentials: bool) -> str:
    cmd = [
        'lk',
        'token',
        'create',
        '--join',
        '--room',
        room_name,
        '--identity',
        identity,
        '--name',
        identity,
        '--valid-for',
        valid_for,
        '--grant',
        '{"canPublish":true,"canPublishData":true,"canSubscribe":true}',
        '--token-only',
        '--yes',
    ]
    if use_dev_credentials:
        cmd.append('--dev')

    try:
        return subprocess.check_output(cmd, text=True).strip()
    except FileNotFoundError as exc:
        raise RuntimeError(
            'Could not find the LiveKit CLI (`lk`) on PATH. Rebuild the devcontainer '
            'or install the LiveKit CLI before using this launch file.'
        ) from exc
    except subprocess.CalledProcessError as exc:
        output = exc.output.strip() if exc.output else ''
        raise RuntimeError(f'Failed to mint LiveKit token with `lk`: {output}') from exc


def _launch_setup(context, *args, **kwargs):
    config_path = Path(LaunchConfiguration('config').perform(context))
    livekit_url = LaunchConfiguration('livekit_url').perform(context)
    identity = LaunchConfiguration('identity').perform(context)
    valid_for = LaunchConfiguration('token_valid_for').perform(context)
    use_dev_credentials = _as_bool(LaunchConfiguration('use_dev_credentials').perform(context))

    room_name = _room_name_from_config(config_path)
    token = _mint_token(room_name, identity, valid_for, use_dev_credentials)

    return [
        SetEnvironmentVariable('LIVEKIT_URL', livekit_url),
        SetEnvironmentVariable('LIVEKIT_TOKEN', token),
        Node(
            package='ros2_livekit_bridge',
            executable='ros2_livekit_bridge_node',
            name='ros2_livekit_bridge',
            namespace=LaunchConfiguration('ns'),
            output='screen',
            parameters=[{'config_path': str(config_path)}],
            arguments=['--ros-args', '--disable-external-lib-logs'],
        ),
    ]


def generate_launch_description():
    default_config = PathJoinSubstitution([
        FindPackageShare('ros2_livekit_bridge'),
        'config',
        'ros2_livekit_bridge.yaml',
    ])

    stub_launch = PathJoinSubstitution([
        FindPackageShare('ros2_livekit_bridge'),
        'launch',
        'stubs',
        'server_stub.launch.py',
    ])

    return LaunchDescription([
        DeclareLaunchArgument('config', default_value=default_config),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('identity', default_value='ros2-livekit-bridge'),
        DeclareLaunchArgument('token_valid_for', default_value='1h'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        DeclareLaunchArgument('ns', default_value='/'),
        DeclareLaunchArgument(
            'service_stub',
            default_value='false',
            description='When true, launch a local std_srvs/srv/SetBool stub server.',
        ),
        DeclareLaunchArgument(
            'stub_service_name',
            default_value='/test/set_bool',
            description='Service name for the SetBool stub when service_stub is true.',
        ),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(stub_launch),
            condition=IfCondition(
                EqualsSubstitution(LaunchConfiguration('service_stub'), 'true')),
            launch_arguments={
                'service_name': LaunchConfiguration('stub_service_name'),
            }.items(),
        ),
        OpaqueFunction(function=_launch_setup),
    ])
