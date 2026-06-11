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

from importlib.util import module_from_spec
from importlib.util import spec_from_file_location
from pathlib import Path

from launch import LaunchContext
import pytest


LAUNCH_FILE = (
    Path(__file__).resolve().parents[2]
    / 'launch'
    / 'livekit_bridge_local.launch.py'
)


def _load_launch_module():
    spec = spec_from_file_location('livekit_bridge_local_launch', LAUNCH_FILE)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_config(path, *, room_name='robo_room'):
    path.write_text(
        f'''ros2_livekit_bridge:
  version: "0.0.1"
  room_name: "{room_name}"
  topic_polling_period_ms: 500
  ros_threads: 4
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
''',
        encoding='utf-8',
    )


def test_room_name_from_config_reads_room_name(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'bridge.yaml'
    _write_config(config_path, room_name='launch_room')

    assert module._room_name_from_config(config_path) == 'launch_room'


def test_room_name_from_config_rejects_missing_room_name(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'bridge.yaml'
    config_path.write_text(
        '''ros2_livekit_bridge:
  version: "0.0.1"
  topic_polling_period_ms: 500
  ros_threads: 4
  topics: []
''',
        encoding='utf-8',
    )

    with pytest.raises(
        RuntimeError,
        match='Could not find ros2_livekit_bridge.room_name',
    ):
        module._room_name_from_config(config_path)


def test_launch_setup_mints_token_and_passes_config_path(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'bridge.yaml'
    _write_config(config_path, room_name='launch_room')
    token_calls = []

    class FakeSetEnvironmentVariable:
        def __init__(self, name, value):
            self.name = name
            self.value = value

    class FakeNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    def fake_mint_token(room_name, identity, valid_for, use_dev_credentials):
        token_calls.append((room_name, identity, valid_for, use_dev_credentials))
        return 'fake-token'

    monkeypatch.setattr(module, 'SetEnvironmentVariable', FakeSetEnvironmentVariable)
    monkeypatch.setattr(module, 'Node', FakeNode)
    monkeypatch.setattr(module, '_mint_token', fake_mint_token)

    context = LaunchContext()
    context.launch_configurations['config'] = str(config_path)
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'test-bridge'
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    actions = module._launch_setup(context)

    assert token_calls == [('launch_room', 'test-bridge', '10m', False)]
    assert actions[0].name == 'LIVEKIT_URL'
    assert actions[0].value == 'ws://example.test:7880'
    assert actions[1].name == 'LIVEKIT_TOKEN'
    assert actions[1].value == 'fake-token'
    assert actions[2].kwargs['package'] == 'ros2_livekit_bridge'
    assert actions[2].kwargs['executable'] == 'ros2_livekit_bridge_node'
    assert actions[2].kwargs['parameters'] == [{'config_path': str(config_path)}]


def test_launch_setup_rejects_missing_config_path(tmp_path):
    module = _load_launch_module()
    missing_config_path = tmp_path / 'missing.yaml'

    context = LaunchContext()
    context.launch_configurations['config'] = str(missing_config_path)
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'test-bridge'
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    with pytest.raises(FileNotFoundError):
        module._launch_setup(context)
