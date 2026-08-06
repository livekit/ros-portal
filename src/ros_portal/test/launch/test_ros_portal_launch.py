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
    / 'ros_portal.launch.py'
)


def _load_launch_module():
    spec = spec_from_file_location('ros_portal_launch', LAUNCH_FILE)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_config(path):
    path.write_text(
        """ros_portal:
  version: "0.0.1"
  ros_threads: 0
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
""",
        encoding='utf-8',
    )


def test_launch_setup_passes_config_path(tmp_path):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    class FakeNode:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    module.Node = FakeNode

    context = LaunchContext()
    context.launch_configurations['config_path'] = str(config_path)

    actions = module._launch_setup(context)

    assert len(actions) == 1
    assert actions[0].kwargs['package'] == 'ros_portal'
    assert actions[0].kwargs['executable'] == 'ros_portal_node'
    assert actions[0].kwargs['parameters'] == [{'config_path': str(config_path)}]


def test_launch_setup_rejects_missing_config_argument(tmp_path, monkeypatch):
    module = _load_launch_module()
    config_path = tmp_path / 'ros_portal.yaml'
    _write_config(config_path)

    monkeypatch.setattr(module, 'Node', lambda **kwargs: None)

    context = LaunchContext()
    context.launch_configurations['config_path'] = ''

    with pytest.raises(RuntimeError, match='The `config_path` launch argument is required.'):
        module._launch_setup(context)


def test_launch_setup_rejects_missing_config_file(tmp_path, monkeypatch):
    module = _load_launch_module()
    missing_config = tmp_path / 'missing.yaml'

    monkeypatch.setattr(module, 'Node', lambda **kwargs: None)

    context = LaunchContext()
    context.launch_configurations['config_path'] = str(missing_config)

    with pytest.raises(RuntimeError, match=f'Config file does not exist: {missing_config}'):
        module._launch_setup(context)
