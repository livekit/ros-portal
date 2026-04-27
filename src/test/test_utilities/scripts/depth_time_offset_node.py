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

import rclpy
from rclpy.node import Node

from sensor_msgs.msg import CompressedImage
from std_msgs.msg import Float64

IMAGE_TOPIC = '/camera/realsense2_camera/depth/image_rect_raw/compressed'


class DepthTimeOffsetNode(Node):

    def __init__(self):
        super().__init__('depth_time_offset_node')

        self.subscription = self.create_subscription(
            CompressedImage,
            IMAGE_TOPIC,
            self.depth_callback,
            10
        )
        print('Subbing to topic: {}'.format(IMAGE_TOPIC))

        self.publisher = self.create_publisher(
            Float64,
            '/depth_time_offset_ms',
            10
        )

        self.get_logger().info('Depth Time Offset Node Started')

    def depth_callback(self, msg: CompressedImage):
        # Current machine time (ROS clock)
        now = self.get_clock().now()

        # Convert message header stamp to rclpy Time
        msg_time = rclpy.time.Time.from_msg(msg.header.stamp)

        # Compute difference and publish it in milliseconds.
        delta = now - msg_time
        delta_ms = delta.nanoseconds / 1e6

        # Publish as Float64
        out_msg = Float64()
        out_msg.data = float(delta_ms)

        self.publisher.publish(out_msg)
        print('diff ms: {}'.format(out_msg.data))


def main(args=None):
    rclpy.init(args=args)
    node = DepthTimeOffsetNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
