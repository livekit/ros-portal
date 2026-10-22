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

"""Conversions between ROS messages and the numpy types of the core modules."""

from builtin_interfaces.msg import Time
from geometry_msgs.msg import Pose
from geometry_msgs.msg import PoseStamped
from geometry_msgs.msg import TransformStamped
from livekit_scarf_slam.transforms import pose_from_transform
from livekit_scarf_slam.transforms import transform_from_pose
from nav_msgs.msg import Odometry
from nav_msgs.msg import Path
import numpy as np
from sensor_msgs.msg import CameraInfo


def time_from_ns(stamp_ns):
    """Return a builtin_interfaces/Time of integer nanoseconds."""
    return Time(sec=stamp_ns // 1_000_000_000, nanosec=stamp_ns % 1_000_000_000)


def ns_from_time(stamp):
    """Return the integer nanoseconds of a builtin_interfaces/Time."""
    return stamp.sec * 1_000_000_000 + stamp.nanosec


def pose_msg(transform):
    """Return a geometry_msgs/Pose of a 4x4 transform."""
    position, orientation = pose_from_transform(transform)
    msg = Pose()
    msg.position.x, msg.position.y, msg.position.z = (float(v) for v in position)
    (msg.orientation.x, msg.orientation.y,
     msg.orientation.z, msg.orientation.w) = (float(v) for v in orientation)
    return msg


def transform_of_pose_msg(msg):
    """Return the 4x4 transform of a geometry_msgs/Pose."""
    return transform_from_pose(
        (msg.position.x, msg.position.y, msg.position.z),
        (msg.orientation.x, msg.orientation.y, msg.orientation.z, msg.orientation.w))


def transform_of_transform_msg(msg):
    """Return the 4x4 transform of a geometry_msgs/Transform."""
    return transform_from_pose(
        (msg.translation.x, msg.translation.y, msg.translation.z),
        (msg.rotation.x, msg.rotation.y, msg.rotation.z, msg.rotation.w))


def transform_stamped_msg(transform, parent_frame, child_frame, stamp):
    """Return a geometry_msgs/TransformStamped of a 4x4 child pose in the parent frame."""
    position, orientation = pose_from_transform(transform)
    msg = TransformStamped()
    msg.header.stamp = stamp
    msg.header.frame_id = parent_frame
    msg.child_frame_id = child_frame
    t = msg.transform
    t.translation.x, t.translation.y, t.translation.z = (float(v) for v in position)
    t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w = (float(v) for v in orientation)
    return msg


def odometry_msg(transform, stamp_ns, frame_id, child_frame_id):
    """Return a nav_msgs/Odometry with the pose of a 4x4 transform."""
    msg = Odometry()
    msg.header.stamp = time_from_ns(stamp_ns)
    msg.header.frame_id = frame_id
    msg.child_frame_id = child_frame_id
    msg.pose.pose = pose_msg(transform)
    return msg


def path_msg(poses, frame_id):
    """
    Return a nav_msgs/Path of (stamp_ns, 4x4 transform) entries.

    The header stamp is the stamp of the last pose.
    """
    msg = Path()
    msg.header.frame_id = frame_id
    for stamp_ns, transform in poses:
        pose = PoseStamped()
        pose.header.stamp = time_from_ns(stamp_ns)
        pose.header.frame_id = frame_id
        pose.pose = pose_msg(transform)
        msg.poses.append(pose)
    if msg.poses:
        msg.header.stamp = msg.poses[-1].header.stamp
    return msg


def rectified_camera_info_msg(projection, size, frame_id):
    """
    Return a sensor_msgs/CameraInfo of a rectified image.

    The image has no distortion and no rectification rotation, so K is the
    left 3x3 block of the projection.
    """
    projection = np.asarray(projection, dtype=float)
    msg = CameraInfo()
    msg.header.frame_id = frame_id
    msg.width, msg.height = int(size[0]), int(size[1])
    msg.distortion_model = 'plumb_bob'
    msg.d = [0.0] * 5
    msg.k = [float(v) for v in projection[:, :3].reshape(-1)]
    msg.r = [float(v) for v in np.eye(3).reshape(-1)]
    msg.p = [float(v) for v in projection.reshape(-1)]
    return msg
