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

"""Publishes ros_portal_msgs/LatencyTimestamps for bridge latency tests.

This is the T0 end of the T0..T5 model: t0 is stamped at publish time on the
sending ROS graph. As the message crosses the bridge it is stamped with T1..T4
(in its own content), and the latency_probe_subscriber on the far graph stamps
T5 and computes the per-segment latencies. Both bridges must run on hosts that
share a wall clock (e.g. one computer), so no clock sync is needed.

The default topic is the bridge's reserved latency topic, which the bridge
forwards automatically when measure_latency is enabled.

Parameters (set with --ros-args -p name:=value):
  topic         ROS topic to publish on (default /ros_portal/latency/timestamp).
  rate_hz       Publish rate in Hz (default 100.0).
  payload_size  Padding bytes added to each message (default 0), to sweep size.
  count         Stop after this many messages (default 0 = run until killed).
"""

import rclpy
from rclpy.node import Node

from ros_portal_msgs.msg import LatencyTimestamps


class LatencyProbePublisher(Node):

    def __init__(self):
        super().__init__('latency_probe_publisher')

        self.topic = self.declare_parameter('topic', '/ros_portal/latency/timestamp').value
        self.rate_hz = float(self.declare_parameter('rate_hz', 100.0).value)
        self.payload_size = int(self.declare_parameter('payload_size', 0).value)
        self.count = int(self.declare_parameter('count', 0).value)

        if self.rate_hz <= 0.0:
            raise ValueError('rate_hz must be > 0')

        self.publisher = self.create_publisher(LatencyTimestamps, self.topic, 10)
        self.padding = bytes(self.payload_size)
        self.seq = 0

        self.timer = self.create_timer(1.0 / self.rate_hz, self.on_tick)
        self.get_logger().info(
            'Publishing LatencyTimestamps on {} at {} Hz, payload_size={} B, count={}'.format(
                self.topic, self.rate_hz, self.payload_size,
                self.count if self.count else 'unlimited'))

    def on_tick(self):
        msg = LatencyTimestamps()
        msg.seq = self.seq
        # T0: publish time on this ROS graph (ROS system clock).
        msg.t0 = self.get_clock().now().to_msg()
        msg.payload = self.padding
        self.publisher.publish(msg)

        self.seq += 1
        if self.count and self.seq >= self.count:
            self.get_logger().info('Published {} messages; stopping.'.format(self.seq))
            self.timer.cancel()
            raise SystemExit


def main(args=None):
    rclpy.init(args=args)
    node = LatencyProbePublisher()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
