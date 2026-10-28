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
    / 'ros_portal_composable.launch.py'
)


def _load_launch_module():
    spec = spec_from_file_location('ros_portal_composable_launch', LAUNCH_FILE)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_config(path):
    path.write_text(
        """ros_portal:
  version: "0.0.1"
  ros_threads: 0
  topics: []
""",
        encoding='utf-8',
    )


def _context(config_path, container_threads='0', container_name='portal_container', namespace='/robot'):
    context = LaunchContext()
    context.launch_configurations['config_path'] = str(config_path)
    context.launch_configurations['container_name'] = container_name
    context.launch_configurations['container_threads'] = container_threads
    context.launch_configurations['ns'] = namespace
    return context


def test_launch_setup_creates_multithreaded_component_container(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    class FakeComposableNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    class FakeComposableNodeContainer:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    monkeypatch.setattr(module, 'ComposableNode', FakeComposableNode)
    monkeypatch.setattr(module, 'ComposableNodeContainer', FakeComposableNodeContainer)

    actions = module._launch_setup(
        _context(config_path, container_threads='4')
    )

    assert len(actions) == 1
    container = actions[0].kwargs
    assert container['package'] == 'rclcpp_components'
    assert container['executable'] == 'component_container_mt'
    assert container['name'] == 'portal_container'
    assert container['parameters'] == [{'thread_num': 4}]

    assert len(container['composable_node_descriptions']) == 1
    component = container['composable_node_descriptions'][0].kwargs
    assert component['package'] == 'ros_portal'
    assert component['plugin'] == 'ros_portal::RosPortalComponent'
    assert component['name'] == 'ros_portal'
    assert component['namespace'] == '/robot'
    assert component['parameters'] == [{'config_path': str(config_path)}]


def test_launch_setup_uses_builtin_config_when_path_is_empty(monkeypatch):
    module = _load_launch_module()

    class FakeComposableNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    class FakeComposableNodeContainer:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    monkeypatch.setattr(module, 'ComposableNode', FakeComposableNode)
    monkeypatch.setattr(module, 'ComposableNodeContainer', FakeComposableNodeContainer)

    actions = module._launch_setup(_context(''))
    component = actions[0].kwargs['composable_node_descriptions'][0].kwargs
    assert component['parameters'] == [{'config_path': ''}]


def test_launch_setup_accepts_single_thread(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    class FakeComposableNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    class FakeComposableNodeContainer:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    monkeypatch.setattr(module, 'ComposableNode', FakeComposableNode)
    monkeypatch.setattr(module, 'ComposableNodeContainer', FakeComposableNodeContainer)

    actions = module._launch_setup(_context(config_path, container_threads='1'))

    assert actions[0].kwargs['parameters'] == [{'thread_num': 1}]


def test_launch_setup_rejects_negative_thread_count(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    with pytest.raises(RuntimeError, match='container_threads must be non-negative'):
        module._launch_setup(_context(config_path, container_threads='-1'))


def test_launch_setup_rejects_non_integer_thread_count(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    with pytest.raises(RuntimeError, match='container_threads must be an integer'):
        module._launch_setup(_context(config_path, container_threads='many'))


def test_launch_setup_rejects_empty_container_name(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    with pytest.raises(RuntimeError, match='container_name launch argument must be non-empty'):
        module._launch_setup(_context(config_path, container_name=''))


def test_launch_setup_rejects_missing_config_file(tmp_path):
    module = _load_launch_module()
    missing_config = tmp_path / 'missing.yaml'

    with pytest.raises(RuntimeError, match=f'Config file does not exist: {missing_config}'):
        module._launch_setup(_context(missing_config))
