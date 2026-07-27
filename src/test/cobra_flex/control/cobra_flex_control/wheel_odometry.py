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

"""Wheel odometry for the Cobra Flex from the driver's ``wheel_states`` feedback.

Subscribes to ``wheel_states`` (``sensor_msgs/JointState``, published by
``cobra_flex_driver`` with the four hub-motor speeds in rad/s), averages each
side, converts to a body twist with differential-drive kinematics, and
integrates the planar pose (exact arc integration). Publishes
``nav_msgs/Odometry`` on ``odom/wheel`` and, optionally, the
``odom -> base_link`` transform.

Skid-steer caveat: the Cobra Flex is a 4-wheel differential (skid-steer)
chassis, so wheel odometry over-reports rotation during in-place turns (the
wheels scrub). Treat yaw from this node as low-trust; when an IMU or laser
odometry is added, fuse them in ``cobra_flex_localization`` and disable
``publish_tf`` here so the EKF owns the transform.
"""

import math

from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import Odometry
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import JointState
from tf2_ros import TransformBroadcaster

from cobra_flex_control.diff_drive import integrate_pose
from cobra_flex_control.diff_drive import wheel_speeds_to_body_twist


class WheelOdometry(Node):
    """Integrates ``wheel_states`` into ``odom/wheel`` (+ optional TF)."""

    def __init__(self) -> None:
        super().__init__('wheel_odometry')

        # Chassis geometry; keep consistent with cobra_flex_driver.
        self.declare_parameter('wheel_radius', 0.03725)
        self.declare_parameter('track_width', 0.228)
        # Joint names per side (matched against the JointState by name, so the
        # driver's ordering is not load-bearing here).
        self.declare_parameter('left_wheel_joints',
                               ['front_left_wheel_joint', 'rear_left_wheel_joint'])
        self.declare_parameter('right_wheel_joints',
                               ['front_right_wheel_joint', 'rear_right_wheel_joint'])
        self.declare_parameter('odom_frame', 'odom')
        self.declare_parameter('base_frame', 'base_link')
        # Disable when an EKF (cobra_flex_localization) owns odom -> base_link.
        self.declare_parameter('publish_tf', True)
        # Placeholder diagonal covariances until bench characterization. Yaw is
        # least trustworthy (skid-steer scrub, see module docstring).
        self.declare_parameter('pose_covariance_diagonal',
                               [0.001, 0.001, 1e6, 1e6, 1e6, 0.05])
        self.declare_parameter('twist_covariance_diagonal',
                               [0.001, 1e6, 1e6, 1e6, 1e6, 0.05])
        # Reject integration steps after feedback gaps (e.g. serial hiccups).
        self.declare_parameter('max_dt', 0.5)

        self._wheel_radius = float(self.get_parameter('wheel_radius').value)
        self._track_width = float(self.get_parameter('track_width').value)
        self._left_joints = set(self.get_parameter('left_wheel_joints').value)
        self._right_joints = set(self.get_parameter('right_wheel_joints').value)
        self._odom_frame = str(self.get_parameter('odom_frame').value)
        self._base_frame = str(self.get_parameter('base_frame').value)
        self._publish_tf = bool(self.get_parameter('publish_tf').value)
        pose_cov = [float(v) for v in self.get_parameter('pose_covariance_diagonal').value]
        twist_cov = [float(v) for v in self.get_parameter('twist_covariance_diagonal').value]
        self._max_dt = float(self.get_parameter('max_dt').value)

        if self._wheel_radius <= 0.0 or self._track_width <= 0.0:
            raise ValueError('wheel_radius and track_width must be > 0')
        for _name, _vec in (('pose_covariance_diagonal', pose_cov),
                            ('twist_covariance_diagonal', twist_cov)):
            if len(_vec) != 6:
                raise ValueError(f'{_name} must have exactly 6 elements, got {len(_vec)}')

        self._pose_covariance = [0.0] * 36
        self._twist_covariance = [0.0] * 36
        for i in range(6):
            self._pose_covariance[i * 7] = pose_cov[i]
            self._twist_covariance[i * 7] = twist_cov[i]

        self._x = 0.0
        self._y = 0.0
        self._yaw = 0.0
        self._last_stamp = None

        self._odom_pub = self.create_publisher(Odometry, 'odom/wheel', qos_profile_sensor_data)
        self._tf_broadcaster = TransformBroadcaster(self) if self._publish_tf else None
        self._sub = self.create_subscription(
            JointState, 'wheel_states', self._on_wheel_states, qos_profile_sensor_data)

        self.get_logger().info(
            f'Publishing wheel odometry on odom/wheel '
            f'({self._odom_frame} -> {self._base_frame}, publish_tf={self._publish_tf})'
        )

    def _side_speed(self, msg: JointState, joints: set) -> float:
        """Mean ground speed (m/s) of the wheels named in ``joints``, or None."""
        speeds = [msg.velocity[i] * self._wheel_radius
                  for i, name in enumerate(msg.name)
                  if name in joints and i < len(msg.velocity)]
        return sum(speeds) / len(speeds) if speeds else None

    def _on_wheel_states(self, msg: JointState) -> None:
        stamp = rclpy.time.Time.from_msg(msg.header.stamp)
        if self._last_stamp is None:
            self._last_stamp = stamp
            return
        dt = (stamp - self._last_stamp).nanoseconds * 1e-9
        self._last_stamp = stamp
        if dt <= 0.0 or dt > self._max_dt:
            # Out-of-order or gapped feedback: skip this integration step.
            return

        v_left = self._side_speed(msg, self._left_joints)
        v_right = self._side_speed(msg, self._right_joints)
        if v_left is None or v_right is None:
            self.get_logger().warn(
                'wheel_states is missing configured left/right joints; check '
                'left_wheel_joints / right_wheel_joints.',
                throttle_duration_sec=10.0)
            return

        v, w = wheel_speeds_to_body_twist(v_left, v_right, self._track_width)
        self._x, self._y, self._yaw = integrate_pose(self._x, self._y, self._yaw, v, w, dt)

        odom = Odometry()
        odom.header.stamp = msg.header.stamp
        odom.header.frame_id = self._odom_frame
        odom.child_frame_id = self._base_frame
        odom.pose.pose.position.x = self._x
        odom.pose.pose.position.y = self._y
        odom.pose.pose.orientation.z = math.sin(self._yaw / 2.0)
        odom.pose.pose.orientation.w = math.cos(self._yaw / 2.0)
        odom.pose.covariance = self._pose_covariance
        odom.twist.twist.linear.x = v
        odom.twist.twist.angular.z = w
        odom.twist.covariance = self._twist_covariance
        self._odom_pub.publish(odom)

        if self._tf_broadcaster is not None:
            tf = TransformStamped()
            tf.header = odom.header
            tf.child_frame_id = self._base_frame
            tf.transform.translation.x = self._x
            tf.transform.translation.y = self._y
            tf.transform.rotation = odom.pose.pose.orientation
            self._tf_broadcaster.sendTransform(tf)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = WheelOdometry()
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
