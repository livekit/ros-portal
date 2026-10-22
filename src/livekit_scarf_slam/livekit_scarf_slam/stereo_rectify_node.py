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
Split and rectify an SBS fisheye stereo JPEG stream for RTAB-Map.

Subscribes to one sensor_msgs/CompressedImage SBS topic and publishes, with
the input stamp:

- <output_namespace>/left/image_rect and right/image_rect (mono8)
- <output_namespace>/left/camera_info and right/camera_info

It also publishes two static transforms: base_frame -> camera_frame (the
optical frame of the left fisheye lens) and camera_frame -> rect_frame (the
rectified left frame). Both rectified images use rect_frame, because RTAB-Map
reads the stereo baseline from the right projection and not from TF.
"""

import cv2
from livekit_scarf_slam.ros_messages import rectified_camera_info_msg
from livekit_scarf_slam.ros_messages import transform_stamped_msg
from livekit_scarf_slam.sbs import decode_grayscale
from livekit_scarf_slam.sbs import frame_scale
from livekit_scarf_slam.sbs import split_sbs
from livekit_scarf_slam.stereo_calibration import load_stereo_calibration
from livekit_scarf_slam.stereo_calibration import rectified_projection
from livekit_scarf_slam.stereo_calibration import rectify_maps
from livekit_scarf_slam.transforms import BODY_TO_OPTICAL_ROTATION
from livekit_scarf_slam.transforms import make_transform
import numpy as np
import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import HistoryPolicy
from rclpy.qos import QoSProfile
from rclpy.qos import ReliabilityPolicy
from sensor_msgs.msg import CameraInfo
from sensor_msgs.msg import CompressedImage
from sensor_msgs.msg import Image
from tf2_ros.static_transform_broadcaster import StaticTransformBroadcaster

_STATS_PERIOD_SEC = 10.0


class StereoRectifyNode(Node):
    """ROS wrapper around the SBS split and the fisheye stereo rectification."""

    def __init__(self):
        super().__init__('stereo_rectify')
        calibration_path = self.declare_parameter('calibration_path', '').value
        input_topic = self.declare_parameter(
            'input_topic', '/stereo/image_raw/compressed').value
        namespace = self.declare_parameter('output_namespace', '/stereo').value.rstrip('/')
        self._reduction = self.declare_parameter('decode_reduction', 2).value
        rect_scale = self.declare_parameter('rect_scale', 0.5).value
        base_frame = self.declare_parameter('base_frame', 'stereo_link').value
        camera_frame = self.declare_parameter('camera_frame', 'cam0_optical').value
        self._rect_frame = self.declare_parameter('rect_frame', 'cam0_rect_optical').value
        if not calibration_path:
            raise ValueError('the calibration_path parameter is required')

        self._calibration = load_stereo_calibration(calibration_path)
        self._rect_scale = rect_scale
        self._frame_size = None
        self._maps = None
        self._frames = 0
        self._dropped = 0

        _, _, rect_size = rectify_maps(self._calibration, rect_scale)
        self._camera_infos = [
            rectified_camera_info_msg(
                rectified_projection(camera, rect_scale), rect_size, self._rect_frame)
            for camera in (self._calibration.left, self._calibration.right)]

        output_qos = QoSProfile(depth=5)
        self._image_pubs = [
            self.create_publisher(Image, f'{namespace}/{side}/image_rect', output_qos)
            for side in ('left', 'right')]
        self._info_pubs = [
            self.create_publisher(CameraInfo, f'{namespace}/{side}/camera_info', output_qos)
            for side in ('left', 'right')]

        stamp = self.get_clock().now().to_msg()
        self._tf_broadcaster = StaticTransformBroadcaster(self)
        self._tf_broadcaster.sendTransform([
            transform_stamped_msg(
                make_transform(BODY_TO_OPTICAL_ROTATION), base_frame, camera_frame, stamp),
            # The rectification rotation maps camera points to rectified points,
            # so the pose of the rectified frame in the camera frame is its inverse.
            transform_stamped_msg(
                make_transform(self._calibration.left.rect_rotation.T),
                camera_frame, self._rect_frame, stamp),
        ])

        # Keep only the newest frame, so that a slow remap never builds a backlog.
        input_qos = QoSProfile(
            depth=1, history=HistoryPolicy.KEEP_LAST,
            reliability=ReliabilityPolicy.BEST_EFFORT)
        self.create_subscription(CompressedImage, input_topic, self._on_frame, input_qos)
        self.create_timer(_STATS_PERIOD_SEC, self._log_stats)
        self.get_logger().info(
            f'Rectifying {input_topic} to {namespace}/{{left,right}}/image_rect '
            f'at {rect_size[0]}x{rect_size[1]}, baseline '
            f'{self._calibration.baseline_m * 1000.0:.1f} mm')

    def _on_frame(self, msg):
        self._frames += 1
        frame = decode_grayscale(msg.data, self._reduction)
        if frame is None:
            self._drop('the JPEG does not decode')
            return
        frame_size = (frame.shape[1], frame.shape[0])
        if frame_size != self._frame_size:
            scale = frame_scale(frame_size, self._calibration.sbs_size)
            if scale is None:
                self._drop(
                    f'frame size {frame_size} is not a scaled copy of the calibrated '
                    f'SBS size {self._calibration.sbs_size}')
                return
            left_maps, right_maps, _ = rectify_maps(
                self._calibration.scaled(scale), self._rect_scale)
            self._maps = (left_maps, right_maps)
            self._frame_size = frame_size
            self.get_logger().info(f'Input frame size is {frame_size}, scale {scale:.3f}')

        for eye, maps, image_pub, info_pub, camera_info in zip(
                split_sbs(frame), self._maps, self._image_pubs, self._info_pubs,
                self._camera_infos):
            rectified = cv2.remap(eye, maps[0], maps[1], cv2.INTER_LINEAR)
            image_pub.publish(_mono8_msg(rectified, msg.header.stamp, self._rect_frame))
            camera_info.header.stamp = msg.header.stamp
            info_pub.publish(camera_info)

    def _drop(self, reason):
        self._dropped += 1
        self.get_logger().warn(f'Dropping frame: {reason}', throttle_duration_sec=5.0)

    def _log_stats(self):
        self.get_logger().info(
            f'{self._frames} frames in the last {_STATS_PERIOD_SEC:.0f} s, '
            f'{self._dropped} dropped')
        self._frames = 0
        self._dropped = 0


def _mono8_msg(image, stamp, frame_id):
    msg = Image()
    msg.header.stamp = stamp
    msg.header.frame_id = frame_id
    msg.height, msg.width = image.shape
    msg.encoding = 'mono8'
    msg.step = msg.width
    msg.data.frombytes(np.ascontiguousarray(image).tobytes())
    return msg


def main(args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = StereoRectifyNode()
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        if node is not None:
            node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
