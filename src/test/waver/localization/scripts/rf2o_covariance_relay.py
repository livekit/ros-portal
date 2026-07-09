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

"""Republish rf2o odometry with sane covariances (and a base-frame twist) for
robot_localization.

rf2o_laser_odometry publishes nav_msgs/Odometry with ALL-ZERO pose and twist
covariances. robot_localization reads a zero variance as "this measurement is
perfectly certain" and over-trusts it, so the EKF cannot blend rf2o against the
IMU -- translation (sourced from rf2o) misbehaves while rotation (sourced from the
gyro, which has real covariances) stays clean.

rf2o also publishes its TWIST in the LASER frame, not the base frame: lin_speed
is taken straight from the scan-match translation (CLaserOdometry2D.cpp:988)
while only the POSE is composed with the laser->base transform. On this robot
the lidar is mounted yaw = pi, so twist.linear.x arrived sign-INVERTED and the
EKF fused a velocity that contradicted the pose deltas (verified on the bench
2026-07-16: +0.05 m/s commanded -> rf2o vx read -0.096). ``twist_frame_yaw``
(the laser's yaw in the base frame; keep equal to the launch's lidar_yaw)
rotates the linear twist into the base frame. Angular z is yaw-invariant.

This relay otherwise copies each rf2o message unchanged except for the
covariance diagonals, which it stamps from parameters, then republishes.
Header, stamp, frames, and pose pass through untouched, so timestamps stay
valid for the filter.

std devs are per-axis: [x, y, z, roll, pitch, yaw] for pose,
[vx, vy, vz, vroll, vpitch, vyaw] for twist. Unfused / 2D-mode axes are given a
large value so they read as "unknown" and never influence the filter.
"""

import math

from nav_msgs.msg import Odometry
import rclpy
from rclpy.node import Node

_BIG = 1e3  # std for axes we do not use (z/roll/pitch); reads as "unknown"


class Rf2oCovarianceRelay(Node):
    """Copy rf2o odometry, injecting diagonal covariances the EKF can weight."""

    def __init__(self) -> None:
        super().__init__('rf2o_covariance_relay')
        self.declare_parameter('input_topic', '/odom_rf2o')
        self.declare_parameter('output_topic', '/odom_rf2o_cov')
        # x, y trusted (scan-match translation); yaw loosely (gyro owns heading).
        self.declare_parameter('pose_stddev', [0.05, 0.05, _BIG, _BIG, _BIG, 0.3])
        self.declare_parameter('twist_stddev', [0.05, 0.05, _BIG, _BIG, _BIG, 0.3])
        # Laser yaw in the base frame (rad); rotates rf2o's laser-frame linear
        # twist into the base frame (see module docstring). Match lidar_yaw.
        self.declare_parameter('twist_frame_yaw', 0.0)

        inp = self.get_parameter('input_topic').value
        outp = self.get_parameter('output_topic').value
        self._pose_cov = self._diag(self.get_parameter('pose_stddev').value)
        self._twist_cov = self._diag(self.get_parameter('twist_stddev').value)
        yaw = float(self.get_parameter('twist_frame_yaw').value)
        self._cos_yaw, self._sin_yaw = math.cos(yaw), math.sin(yaw)

        self._pub = self.create_publisher(Odometry, outp, 10)
        self._sub = self.create_subscription(Odometry, inp, self._on_odom, 10)
        self.get_logger().info(
            f'Relaying {inp} -> {outp} with injected pose/twist covariances'
        )

    @staticmethod
    def _diag(stddev) -> list:
        cov = [0.0] * 36
        for i, s in enumerate(stddev):
            cov[i * 6 + i] = float(s) * float(s)
        return cov

    def _on_odom(self, msg: Odometry) -> None:
        msg.pose.covariance = self._pose_cov
        msg.twist.covariance = self._twist_cov
        # Rotate the laser-frame linear twist into the base frame (2D).
        vx, vy = msg.twist.twist.linear.x, msg.twist.twist.linear.y
        msg.twist.twist.linear.x = self._cos_yaw * vx - self._sin_yaw * vy
        msg.twist.twist.linear.y = self._sin_yaw * vx + self._cos_yaw * vy
        self._pub.publish(msg)


def main(args=None) -> None:
    rclpy.init(args=args)
    node = Rf2oCovarianceRelay()
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
