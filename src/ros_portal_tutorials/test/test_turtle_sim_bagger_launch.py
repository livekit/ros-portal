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

from datetime import datetime
from datetime import timezone
from importlib.util import module_from_spec
from importlib.util import spec_from_file_location
from pathlib import Path

from launch import LaunchContext
from launch.utilities import perform_substitutions
import pytest


LAUNCH_FILE = (
    Path(__file__).resolve().parents[1]
    / 'launch'
    / 'turtle_sim_bagger.launch.py'
)


def _load_launch_module():
    spec = spec_from_file_location('turtle_sim_bagger_launch', LAUNCH_FILE)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize(
    ('room_name', 'expected'),
    [
        ('turtle_room', 'turtle_room'),
        ('fleet.alpha-1', 'fleet.alpha-1'),
        (' site/robot #1 ', 'site_robot_1'),
        ('../../fleet', 'fleet'),
    ],
)
def test_sanitize_room_name(room_name, expected):
    module = _load_launch_module()

    assert module._sanitize_room_name(room_name) == expected


@pytest.mark.parametrize('room_name', ['', '   ', '...'])
def test_sanitize_room_name_rejects_empty_bag_name(room_name):
    module = _load_launch_module()

    with pytest.raises(RuntimeError, match='room_name'):
        module._sanitize_room_name(room_name)


def test_make_bag_output_path_uses_room_and_utc_timestamp(tmp_path):
    module = _load_launch_module()
    timestamp = datetime(2026, 8, 10, 18, 45, 12, 123456, tzinfo=timezone.utc)
    output_dir = tmp_path / 'nested' / 'bags'

    output_path = module._make_bag_output_path(
        'turtle_room',
        str(output_dir),
        timestamp,
    )

    assert output_path == output_dir / 'turtle_room_20260810T184512123456Z'
    assert output_dir.is_dir()
    assert not output_path.exists()


def test_make_bag_output_path_rejects_empty_output_dir():
    module = _load_launch_module()

    with pytest.raises(RuntimeError, match='bag_output_dir'):
        module._make_bag_output_path('turtle_room', ' ')


def test_launch_setup_records_all_topics_to_room_named_bag(tmp_path):
    module = _load_launch_module()
    context = LaunchContext()
    context.launch_configurations.update({
        'room_name': 'turtle_room',
        'identity': 'bagger',
        'bag_output_dir': str(tmp_path),
        'livekit_url': 'ws://example.test:7880',
        'token': 'provided-token',
        'token_valid_for': '1h',
        'use_dev_credentials': 'false',
    })

    actions = module._launch_setup(context)
    command = [perform_substitutions(context, item) for item in actions[2].cmd]

    assert command[:5] == ['ros2', 'bag', 'record', '-a', '-o']
    assert Path(command[5]).parent == tmp_path
    assert Path(command[5]).name.startswith('turtle_room_')
