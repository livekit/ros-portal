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
import subprocess

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import OpaqueFunction
from launch.actions import SetEnvironmentVariable
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def _as_bool(value: str) -> bool:
    return value.strip().lower() in ('1', 'true', 'yes', 'on')


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
    if not config_path.is_file():
        raise RuntimeError(f'Config file does not exist: {config_path}')
    livekit_url = LaunchConfiguration('livekit_url').perform(context)
    identity = LaunchConfiguration('identity').perform(context)
    room_name = LaunchConfiguration('room_name').perform(context).strip()
    valid_for = LaunchConfiguration('token_valid_for').perform(context)
    use_dev_credentials = _as_bool(LaunchConfiguration('use_dev_credentials').perform(context))
    provided_token = LaunchConfiguration('token').perform(context).strip()
    if provided_token:
        token = provided_token
    else:
        if not room_name:
            raise RuntimeError('room_name launch argument must be non-empty when minting a token')
        token = _mint_token(room_name, identity, valid_for, use_dev_credentials)

    bridge_node = Node(
        package='ros2_livekit_bridge',
        executable='ros2_livekit_bridge_node',
        name='ros2_livekit_bridge',
        namespace=LaunchConfiguration('ns'),
        output='screen',
        parameters=[{'config_path': str(config_path)}],
        arguments=['--ros-args', '--disable-external-lib-logs'],
    )

    return [
        SetEnvironmentVariable('LIVEKIT_URL', livekit_url),
        SetEnvironmentVariable('LIVEKIT_TOKEN', token),
        bridge_node,
    ]


def generate_launch_description():
    default_config = PathJoinSubstitution([
        FindPackageShare('ros2_livekit_bridge'),
        'config',
        'ros2_livekit_bridge.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument('config', default_value=default_config),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument('identity', default_value='ros2-livekit-bridge'),
        DeclareLaunchArgument(
            'room_name',
            default_value='robo_room',
            description='LiveKit room used when minting a token via `lk`. Ignored when token is set.',
        ),
        DeclareLaunchArgument(
            'token',
            default_value='',
            description='Optional LiveKit JWT. When set, skips minting via `lk`.',
        ),
        DeclareLaunchArgument('token_valid_for', default_value='1h'),
        DeclareLaunchArgument('use_dev_credentials', default_value='true'),
        DeclareLaunchArgument('ns', default_value='/'),
        OpaqueFunction(function=_launch_setup),
    ])
