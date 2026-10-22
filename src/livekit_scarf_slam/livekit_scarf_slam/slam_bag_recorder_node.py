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
Record the ScaRF-SLAM input bag from RTAB-Map.

Each new RTAB-Map graph node becomes one keyframe. The node writes the cam0
fisheye image (the left half of the matching SBS frame), the odometry pose, and
a trajectory snapshot of all keyframes. On shutdown, it writes the final
trajectory and closes the bag.
"""

from livekit_scarf_slam.bag_writer import SlamBagWriter
from livekit_scarf_slam.ros_messages import ns_from_time
from livekit_scarf_slam.ros_messages import odometry_msg
from livekit_scarf_slam.ros_messages import path_msg
from livekit_scarf_slam.ros_messages import time_from_ns
from livekit_scarf_slam.ros_messages import transform_of_pose_msg
from livekit_scarf_slam.ros_messages import transform_of_transform_msg
from livekit_scarf_slam.sbs import left_eye_jpeg
from livekit_scarf_slam.slam_bag import FINAL_TRAJECTORY_TOPIC
from livekit_scarf_slam.slam_bag import GraphNode
from livekit_scarf_slam.slam_bag import IMAGE_TOPIC
from livekit_scarf_slam.slam_bag import KeyframeTracker
from livekit_scarf_slam.slam_bag import ODOMETRY_TOPIC
from livekit_scarf_slam.slam_bag import seconds_to_ns
from livekit_scarf_slam.slam_bag import StampBuffer
from livekit_scarf_slam.slam_bag import TRAJECTORY_TOPIC
from livekit_scarf_slam.stereo_calibration import load_stereo_calibration
import rclpy
from rclpy.duration import Duration
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import QoSProfile
from rclpy.qos import ReliabilityPolicy
from rclpy.time import Time
from rtabmap_msgs.msg import MapData
from sensor_msgs.msg import CompressedImage
from tf2_ros import TransformException
from tf2_ros.buffer import Buffer
from tf2_ros.transform_listener import TransformListener


class SlamBagRecorderNode(Node):
    """ROS wrapper that feeds RTAB-Map MapData to a KeyframeTracker and writes the bag."""

    def __init__(self):
        super().__init__('slam_bag_recorder')
        calibration_path = self.declare_parameter('calibration_path', '').value
        sbs_topic = self.declare_parameter('sbs_topic', '/stereo/image_raw/compressed').value
        map_data_topic = self.declare_parameter('map_data_topic', '/rtabmap/mapData').value
        output_path = self.declare_parameter('output_path', '').value
        self._base_frame = self.declare_parameter('base_frame', 'stereo_link').value
        self._camera_frame = self.declare_parameter('camera_frame', 'cam0_optical').value
        self._map_frame = self.declare_parameter('map_frame', 'map').value
        self._odom_frame = self.declare_parameter('odom_frame', 'odom').value
        buffer_sec = self.declare_parameter('image_buffer_sec', 5.0).value
        tolerance_ms = self.declare_parameter('stamp_tolerance_ms', 1.0).value
        self._jpeg_quality = self.declare_parameter('jpeg_quality', 95).value
        if not calibration_path or not output_path:
            raise ValueError('the calibration_path and output_path parameters are required')

        self._eye_size = load_stereo_calibration(calibration_path).image_size
        self._tolerance_ns = int(tolerance_ms * 1e6)
        self._images = StampBuffer(int(buffer_sec * 1e9))
        self._tracker = None
        self._writer = SlamBagWriter(output_path)
        self._output_path = output_path

        self._tf_buffer = Buffer()
        self._tf_listener = TransformListener(self._tf_buffer, self)

        self.create_subscription(
            CompressedImage, sbs_topic, self._on_image,
            QoSProfile(depth=10, reliability=ReliabilityPolicy.BEST_EFFORT))
        self.create_subscription(MapData, map_data_topic, self._on_map_data, QoSProfile(depth=10))
        self.get_logger().info(f'Recording the ScaRF-SLAM input bag to {output_path}')

    def _on_image(self, msg):
        self._images.add(ns_from_time(msg.header.stamp), msg)

    def _on_map_data(self, msg):
        if self._tracker is None and not self._create_tracker():
            return
        graph = msg.graph
        nodes = [
            GraphNode(node_id=n.id, stamp_ns=seconds_to_ns(n.stamp),
                      odom_pose=transform_of_pose_msg(n.pose))
            for n in msg.nodes]
        graph_poses = {
            node_id: transform_of_pose_msg(pose)
            for node_id, pose in zip(graph.poses_id, graph.poses)}
        keyframes = self._tracker.update(
            nodes, graph_poses, transform_of_transform_msg(graph.map_to_odom),
            self._images.nearest)
        if not keyframes:
            return

        for keyframe in keyframes:
            jpeg = left_eye_jpeg(keyframe.image.data, self._eye_size, self._jpeg_quality)
            if jpeg is None:
                self.get_logger().warn(f'Keyframe {keyframe.node_id}: the JPEG does not decode')
                continue
            self._writer.write(
                ODOMETRY_TOPIC,
                odometry_msg(keyframe.odom_pose, keyframe.stamp_ns,
                             self._odom_frame, self._camera_frame),
                keyframe.stamp_ns)
            image = CompressedImage()
            image.header.stamp = time_from_ns(keyframe.stamp_ns)
            image.header.frame_id = self._camera_frame
            image.format = 'jpeg'
            image.data.frombytes(jpeg)
            self._writer.write(IMAGE_TOPIC, image, keyframe.stamp_ns)

        snapshot = path_msg(self._tracker.snapshot(), self._map_frame)
        self._writer.write(TRAJECTORY_TOPIC, snapshot, ns_from_time(snapshot.header.stamp))
        self.get_logger().info(
            f'{self._tracker.keyframe_count} keyframes, '
            f'{self._tracker.skipped_nodes} graph nodes without a matching image',
            throttle_duration_sec=10.0)

    def _create_tracker(self):
        try:
            transform = self._tf_buffer.lookup_transform(
                self._base_frame, self._camera_frame, Time(), Duration(seconds=0.0))
        except TransformException as error:
            self.get_logger().warn(
                f'Waiting for the {self._base_frame} -> {self._camera_frame} transform: {error}',
                throttle_duration_sec=5.0)
            return False
        self._tracker = KeyframeTracker(
            transform_of_transform_msg(transform.transform), self._tolerance_ns)
        return True

    def close(self):
        """Write the final trajectory and close the bag."""
        if self._tracker is not None and self._tracker.keyframe_count:
            final = path_msg(self._tracker.snapshot(), self._map_frame)
            self._writer.write(FINAL_TRAJECTORY_TOPIC, final, ns_from_time(final.header.stamp))
        self._writer.close()
        keyframes = self._tracker.keyframe_count if self._tracker is not None else 0
        self.get_logger().info(f'Closed {self._output_path} with {keyframes} keyframes')


def main(args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = SlamBagRecorderNode()
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        if node is not None:
            node.close()
            node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
