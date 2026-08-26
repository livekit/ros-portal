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
    / 'ros_portal_local.launch.py'
)


def _load_launch_module():
    spec = spec_from_file_location('ros_portal_local_launch', LAUNCH_FILE)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_config(path):
    path.write_text(
        """ros_portal:
  version: "0.0.1"
  topic_polling_period_ms: 500
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
""",
        encoding='utf-8',
    )


def test_mint_token_allows_participant_attribute_updates(monkeypatch):
    module = _load_launch_module()
    token_commands = []

    def fake_check_output(command, text):
        token_commands.append((command, text))
        return 'fake-token\n'

    monkeypatch.setattr(module.subprocess, 'check_output', fake_check_output)

    assert module._mint_token('launch_room', 'ros-portal-test', '10m', False) == 'fake-token'
    assert len(token_commands) == 1
    command, text = token_commands[0]
    assert text is True
    assert '--allow-update-metadata' in command


def test_launch_setup_mints_token_and_passes_config_path(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)
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
    context.launch_configurations['config_path'] = str(config_path)
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'ros-portal-test'
    context.launch_configurations['room_name'] = 'launch_room'
    context.launch_configurations['token'] = ''
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    actions = module._launch_setup(context)

    assert token_calls == [('launch_room', 'ros-portal-test', '10m', False)]
    assert actions[0].name == 'RUST_LOG'
    assert actions[0].value == 'info'
    assert actions[1].name == 'LIVEKIT_URL'
    assert actions[1].value == 'ws://example.test:7880'
    assert actions[2].name == 'LIVEKIT_TOKEN'
    assert actions[2].value == 'fake-token'
    assert actions[3].kwargs['package'] == 'ros_portal'
    assert actions[3].kwargs['executable'] == 'ros_portal_node'
    assert actions[3].kwargs['parameters'] == [{'config_path': str(config_path)}]


def test_launch_setup_uses_provided_token_without_minting(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)
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
        return 'should-not-be-used'

    monkeypatch.setattr(module, 'SetEnvironmentVariable', FakeSetEnvironmentVariable)
    monkeypatch.setattr(module, 'Node', FakeNode)
    monkeypatch.setattr(module, '_mint_token', fake_mint_token)

    context = LaunchContext()
    context.launch_configurations['config_path'] = str(config_path)
    context.launch_configurations['livekit_url'] = 'wss://example.livekit.cloud'
    context.launch_configurations['identity'] = 'custom-id'
    context.launch_configurations['room_name'] = 'ignored-room'
    context.launch_configurations['token'] = 'provided-jwt-token'
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'true'

    actions = module._launch_setup(context)

    assert token_calls == []
    assert actions[0].name == 'RUST_LOG'
    assert actions[0].value == 'info'
    assert actions[1].name == 'LIVEKIT_URL'
    assert actions[1].value == 'wss://example.livekit.cloud'
    assert actions[2].name == 'LIVEKIT_TOKEN'
    assert actions[2].value == 'provided-jwt-token'
    assert actions[3].kwargs['parameters'] == [{'config_path': str(config_path)}]


def test_launch_setup_uses_builtin_default_when_config_path_empty(tmp_path, monkeypatch):
    module = _load_launch_module()

    class FakeSetEnvironmentVariable:
        def __init__(self, name, value):
            self.name = name
            self.value = value

    class FakeNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    monkeypatch.setattr(module, 'SetEnvironmentVariable', FakeSetEnvironmentVariable)
    monkeypatch.setattr(module, 'Node', FakeNode)
    monkeypatch.setattr(module, '_mint_token', lambda *args, **kwargs: 'unused')

    context = LaunchContext()
    context.launch_configurations['config_path'] = ''
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'ros-portal-test'
    context.launch_configurations['room_name'] = 'launch_room'
    context.launch_configurations['token'] = 'provided-jwt-token'
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    actions = module._launch_setup(context)

    assert actions[3].kwargs['parameters'] == [{'config_path': ''}]


def test_launch_setup_rejects_missing_config_file(tmp_path, monkeypatch):
    module = _load_launch_module()
    missing_config = tmp_path / 'missing.yaml'

    monkeypatch.setattr(module, '_mint_token', lambda *args, **kwargs: 'unused')

    context = LaunchContext()
    context.launch_configurations['config_path'] = str(missing_config)
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'ros-portal-test'
    context.launch_configurations['room_name'] = 'launch_room'
    context.launch_configurations['token'] = ''
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    with pytest.raises(RuntimeError, match=f'Config file does not exist: {missing_config}'):
        module._launch_setup(context)


def test_launch_setup_rejects_empty_room_name_when_minting(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    monkeypatch.setattr(module, '_mint_token', lambda *args, **kwargs: 'unused')

    context = LaunchContext()
    context.launch_configurations['config_path'] = str(config_path)
    context.launch_configurations['livekit_url'] = 'ws://example.test:7880'
    context.launch_configurations['identity'] = 'ros-portal-test'
    context.launch_configurations['room_name'] = ''
    context.launch_configurations['token'] = ''
    context.launch_configurations['token_valid_for'] = '10m'
    context.launch_configurations['use_dev_credentials'] = 'false'

    with pytest.raises(RuntimeError, match='room_name launch argument must be non-empty'):
        module._launch_setup(context)
