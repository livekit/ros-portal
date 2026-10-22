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

"""
Keyframe bookkeeping for the ScaRF-SLAM input bag.

ScaRF-SLAM reads one bag with four topics. For each keyframe, the bag has the
cam0 image, the odometry pose (without loop closure), and a trajectory snapshot
of all keyframes so far (with loop closure). At the end, the bag has one final
trajectory. All poses are cam0 poses, stamped with the image stamp.

This module has no ROS imports, so the tests run without a ROS graph.
"""

import bisect
from dataclasses import dataclass

import numpy as np

# ScaRF-SLAM default input topics (scarf_slam/integrations/slam_bag.py).
TRAJECTORY_TOPIC = '/scarf_slam/input/trajectory'
FINAL_TRAJECTORY_TOPIC = '/scarf_slam/input/trajectory_final'
ODOMETRY_TOPIC = '/scarf_slam/input/odometry'
IMAGE_TOPIC = '/scarf_slam/input/cam0/image_raw/compressed'


def seconds_to_ns(stamp_sec):
    """Convert a float stamp in seconds, as RTAB-Map stores it, to integer nanoseconds."""
    return int(round(stamp_sec * 1e9))


class StampBuffer:
    """Recent items by stamp in nanoseconds, kept for a fixed time horizon."""

    def __init__(self, horizon_ns):
        self._horizon_ns = horizon_ns
        self._stamps = []
        self._items = []

    def __len__(self):
        return len(self._stamps)

    def add(self, stamp_ns, item):
        """Add an item, and drop the items older than the horizon before the newest stamp."""
        index = bisect.bisect_right(self._stamps, stamp_ns)
        self._stamps.insert(index, stamp_ns)
        self._items.insert(index, item)
        cutoff = bisect.bisect_left(self._stamps, self._stamps[-1] - self._horizon_ns)
        del self._stamps[:cutoff]
        del self._items[:cutoff]

    def nearest(self, stamp_ns, tolerance_ns):
        """Return (stamp_ns, item) nearest to stamp_ns within tolerance_ns, or None."""
        index = bisect.bisect_left(self._stamps, stamp_ns)
        best = None
        for candidate in (index - 1, index):
            if 0 <= candidate < len(self._stamps):
                delta = abs(self._stamps[candidate] - stamp_ns)
                if delta <= tolerance_ns and (best is None or delta < best[0]):
                    best = (delta, candidate)
        if best is None:
            return None
        return self._stamps[best[1]], self._items[best[1]]


@dataclass(frozen=True)
class GraphNode:
    """One RTAB-Map graph node from MapData."""

    node_id: int
    stamp_ns: int
    odom_pose: np.ndarray
    """4x4 pose of the base frame in the odometry frame when the node was added."""


@dataclass(frozen=True)
class Keyframe:
    """One keyframe to write to the bag."""

    node_id: int
    stamp_ns: int
    """Stamp of the matched image. All records of this keyframe use it."""
    odom_pose: np.ndarray
    """4x4 pose of cam0 in the odometry frame."""
    image: object
    """The image that image_lookup returned."""


class KeyframeTracker:
    """
    Turn RTAB-Map graph updates into ScaRF-SLAM keyframes.

    RTAB-Map poses are base frame poses. The tracker multiplies them by the
    static base-to-cam0 transform, so that all output poses are cam0 poses.
    """

    def __init__(self, base_to_camera, stamp_tolerance_ns):
        self._base_to_camera = np.asarray(base_to_camera, dtype=float)
        self._tolerance_ns = stamp_tolerance_ns
        self._seen = set()
        self._stamps = {}
        self._map_poses = {}
        self.skipped_nodes = 0

    @property
    def keyframe_count(self):
        """Return the number of accepted keyframes."""
        return len(self._stamps)

    def update(self, nodes, graph_poses, map_to_odom, image_lookup):
        """
        Process one MapData message, and return the new keyframes in node order.

        nodes: GraphNode list of the message.
        graph_poses: dict of node id to the optimized 4x4 base pose in the map frame.
        map_to_odom: 4x4 transform of the odometry frame in the map frame.
        image_lookup: callable(stamp_ns, tolerance_ns) that returns
            (image_stamp_ns, image) or None.

        A new node without a matching image is skipped and is never written.
        """
        new_keyframes = []
        for node in sorted(nodes, key=lambda n: n.node_id):
            if node.node_id in self._seen:
                continue
            self._seen.add(node.node_id)
            match = image_lookup(node.stamp_ns, self._tolerance_ns)
            if match is None:
                self.skipped_nodes += 1
                continue
            image_stamp_ns, image = match
            self._stamps[node.node_id] = image_stamp_ns
            self._map_poses[node.node_id] = map_to_odom @ node.odom_pose
            new_keyframes.append(Keyframe(
                node_id=node.node_id,
                stamp_ns=image_stamp_ns,
                odom_pose=node.odom_pose @ self._base_to_camera,
                image=image))
        for node_id, pose in graph_poses.items():
            if node_id in self._stamps:
                self._map_poses[node_id] = np.asarray(pose, dtype=float)
        return new_keyframes

    def snapshot(self):
        """
        Return the trajectory of all keyframes as (stamp_ns, 4x4 cam0 pose in the map frame).

        A keyframe that is not in the latest graph keeps its last known pose.
        """
        return sorted(
            ((self._stamps[node_id], pose @ self._base_to_camera)
             for node_id, pose in self._map_poses.items()),
            key=lambda entry: entry[0])
