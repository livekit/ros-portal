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

"""Prefix the frame_ids of Gazebo-emitted TF and scan messages for one robot.

On the real robot every frame_id is already prefixed with ``<robot_name>/``
(robot_state_publisher's frame_prefix, the rf2o odom frames, the static laser
TF), so slam_toolbox -- whose map/odom/base frames are rewritten to the same
prefix by slam.launch.py -- has a connected TF tree, and several robots can be
merged onto one viewer's /tf without frame collisions.

In simulation the equivalent frames come from Gazebo plugins whose frame names
are baked into the (third-party, vcs-managed) URDF and cannot be prefixed there:

  * the DiffDrive plugin publishes ``odom`` -> ``base_footprint`` on /tf
  * the gpu_lidar sensor stamps /scan with ``lidar_link``

robot_state_publisher's frame_prefix only prefixes the URDF-derived frames, so
without this relay the Gazebo frames stay unprefixed and slam's
``<robot_name>/odom`` / ``<robot_name>/base_footprint`` tree is disconnected --
mapping silently never works. A plain identity bridge (static
``<robot_name>/odom`` -> ``odom``) would reconnect the tree but leave the bare
``odom``/``lidar_link`` frames in the graph, which collide across robots and
defeat the multi-robot goal, so we rewrite the frame_ids in place instead.

The node subscribes to the raw (unprefixed) Gazebo topics and republishes each
message onto the canonical topic with ``<prefix>`` prepended to every frame_id.
It only rewrites frames that are not already prefixed, so it is idempotent and
safe if a message somehow arrives pre-prefixed.

This script is needed to avoid forking the src/externals/waver repo.
"""

import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy
from rclpy.qos import HistoryPolicy
from rclpy.qos import QoSProfile
from rclpy.qos import ReliabilityPolicy
from sensor_msgs.msg import LaserScan
from tf2_msgs.msg import TFMessage


class SimFramePrefixRelay(Node):
    def __init__(self):
        super().__init__('sim_frame_prefix_relay')

        # A trailing-slash prefix, e.g. "robot_1/". Empty -> passthrough.
        raw_prefix = self.declare_parameter('prefix', '').value
        self.prefix = raw_prefix if (not raw_prefix or raw_prefix.endswith('/')) else f'{raw_prefix}/'

        in_tf = self.declare_parameter('input_tf_topic', '/sim/tf').value
        out_tf = self.declare_parameter('output_tf_topic', '/tf').value
        in_scan = self.declare_parameter('input_scan_topic', '/sim/scan').value
        out_scan = self.declare_parameter('output_scan_topic', '/scan').value

        # /tf: reliable, volatile, deep history (matches the tf2 convention and
        # ros_gz_bridge's reliable default).
        tf_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=100,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
        )
        # /scan: reliable to stay compatible with slam_toolbox's scan
        # subscription (which is reliable by default); ros_gz_bridge publishes
        # /scan reliable, so a reliable subscription here connects to it.
        scan_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
        )

        self._tf_pub = self.create_publisher(TFMessage, out_tf, tf_qos)
        self._scan_pub = self.create_publisher(LaserScan, out_scan, scan_qos)
        self.create_subscription(TFMessage, in_tf, self._on_tf, tf_qos)
        self.create_subscription(LaserScan, in_scan, self._on_scan, scan_qos)

        self.get_logger().info(
            f"Relaying '{in_tf}' -> '{out_tf}' and '{in_scan}' -> '{out_scan}' "
            f"with frame prefix '{self.prefix or '(none)'}'"
        )

    def _prefixed(self, frame_id: str) -> str:
        if not self.prefix or not frame_id or frame_id.startswith(self.prefix):
            return frame_id
        return f'{self.prefix}{frame_id}'

    def _on_tf(self, msg: TFMessage):
        for transform in msg.transforms:
            transform.header.frame_id = self._prefixed(transform.header.frame_id)
            transform.child_frame_id = self._prefixed(transform.child_frame_id)
        self._tf_pub.publish(msg)

    def _on_scan(self, msg: LaserScan):
        msg.header.frame_id = self._prefixed(msg.header.frame_id)
        self._scan_pub.publish(msg)


def main():
    rclpy.init()
    node = SimFramePrefixRelay()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
