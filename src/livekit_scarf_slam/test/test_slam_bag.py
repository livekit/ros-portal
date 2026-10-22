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

from livekit_scarf_slam.slam_bag import GraphNode
from livekit_scarf_slam.slam_bag import KeyframeTracker
from livekit_scarf_slam.slam_bag import seconds_to_ns
from livekit_scarf_slam.slam_bag import StampBuffer
from livekit_scarf_slam.transforms import BODY_TO_OPTICAL_ROTATION
from livekit_scarf_slam.transforms import make_transform
import numpy as np
import pytest

BASE_TO_CAMERA = make_transform(BODY_TO_OPTICAL_ROTATION, [0.1, 0.03, 0.2])
TOLERANCE_NS = 1_000_000
T0 = 1_760_000_000_000_000_000


def _translation(x, y=0.0):
    return make_transform(translation=[x, y, 0.0])


def _node(node_id, stamp_ns, x):
    # RTAB-Map stores stamps as float seconds, which loses some nanoseconds.
    return GraphNode(node_id=node_id, stamp_ns=seconds_to_ns(stamp_ns / 1e9),
                     odom_pose=_translation(x))


def _images(*stamps):
    buffer = StampBuffer(horizon_ns=10_000_000_000)
    for stamp in stamps:
        buffer.add(stamp, f'image@{stamp}')
    return buffer


def test_seconds_to_ns_is_close():
    assert abs(seconds_to_ns((T0 + 123_456_789) / 1e9) - (T0 + 123_456_789)) < 1000


def test_stamp_buffer_nearest_and_horizon():
    buffer = StampBuffer(horizon_ns=100)
    for stamp in (10, 50, 30, 120):
        buffer.add(stamp, stamp)
    # 10 is older than 120 - 100 and is dropped.
    assert len(buffer) == 3
    assert buffer.nearest(10, 5) is None
    assert buffer.nearest(33, 5) == (30, 30)
    assert buffer.nearest(45, 10) == (50, 50)
    assert buffer.nearest(119, 0) is None


def test_new_nodes_become_keyframes_with_image_stamps():
    stamps = [T0, T0 + 333_000_001]
    images = _images(*stamps)
    tracker = KeyframeTracker(BASE_TO_CAMERA, TOLERANCE_NS)
    nodes = [_node(2, stamps[1], 1.0), _node(1, stamps[0], 0.0)]
    graph = {1: _translation(0.0), 2: _translation(1.0)}

    keyframes = tracker.update(nodes, graph, np.eye(4), images.nearest)

    assert [k.node_id for k in keyframes] == [1, 2]
    assert [k.stamp_ns for k in keyframes] == stamps
    assert keyframes[1].image == f'image@{stamps[1]}'
    np.testing.assert_allclose(keyframes[1].odom_pose, _translation(1.0) @ BASE_TO_CAMERA)
    snapshot = tracker.snapshot()
    assert [stamp for stamp, _ in snapshot] == stamps
    np.testing.assert_allclose(snapshot[1][1], _translation(1.0) @ BASE_TO_CAMERA)


def test_loop_closure_updates_the_snapshot_but_not_the_odometry():
    stamps = [T0, T0 + 400_000_000, T0 + 800_000_000]
    images = _images(*stamps)
    tracker = KeyframeTracker(BASE_TO_CAMERA, TOLERANCE_NS)
    tracker.update([_node(1, stamps[0], 0.0), _node(2, stamps[1], 1.0)],
                   {1: _translation(0.0), 2: _translation(1.0)}, np.eye(4), images.nearest)

    # MapData repeats node 2 and adds node 3. The optimizer moved node 2.
    keyframes = tracker.update(
        [_node(2, stamps[1], 1.0), _node(3, stamps[2], 2.0)],
        {1: _translation(0.0), 2: _translation(0.9, 0.1), 3: _translation(1.8)},
        np.eye(4), images.nearest)

    assert [k.node_id for k in keyframes] == [3]
    np.testing.assert_allclose(keyframes[0].odom_pose, _translation(2.0) @ BASE_TO_CAMERA)
    snapshot = dict(tracker.snapshot())
    np.testing.assert_allclose(snapshot[stamps[1]], _translation(0.9, 0.1) @ BASE_TO_CAMERA)
    np.testing.assert_allclose(snapshot[stamps[2]], _translation(1.8) @ BASE_TO_CAMERA)


def test_node_without_image_is_skipped_once():
    images = _images(T0)
    tracker = KeyframeTracker(BASE_TO_CAMERA, TOLERANCE_NS)
    missing = _node(1, T0 + 50_000_000, 0.0)

    assert tracker.update([missing], {1: _translation(0.0)}, np.eye(4), images.nearest) == []
    images.add(T0 + 50_000_000, 'late image')
    assert tracker.update([missing], {1: _translation(0.0)}, np.eye(4), images.nearest) == []
    assert tracker.skipped_nodes == 1
    assert tracker.keyframe_count == 0
    assert tracker.snapshot() == []


def test_node_outside_the_graph_uses_map_to_odom():
    images = _images(T0)
    tracker = KeyframeTracker(BASE_TO_CAMERA, TOLERANCE_NS)
    map_to_odom = _translation(5.0)

    tracker.update([_node(1, T0, 1.0)], {}, map_to_odom, images.nearest)

    (stamp, pose), = tracker.snapshot()
    assert stamp == T0
    np.testing.assert_allclose(pose, _translation(6.0) @ BASE_TO_CAMERA)


@pytest.mark.parametrize('offset_ns, accepted', [(900_000, True), (1_100_000, False)])
def test_stamp_tolerance(offset_ns, accepted):
    images = _images(T0 + offset_ns)
    tracker = KeyframeTracker(BASE_TO_CAMERA, TOLERANCE_NS)
    keyframes = tracker.update([_node(1, T0, 0.0)], {}, np.eye(4), images.nearest)
    assert bool(keyframes) == accepted
