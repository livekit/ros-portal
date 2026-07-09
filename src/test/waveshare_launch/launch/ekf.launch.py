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

"""Bring up the robot_localization EKF with TF frames prefixed for one robot.

The EKF publishes the ``odom -> base_footprint`` transform plus ``/odom`` (remapped
from ``odometry/filtered``). rf2o runs with ``publish_tf:=false`` so the EKF is the
sole owner of that transform, and slam_toolbox -- which reads the transform, not the
topic -- transparently maps against it.

Two params files select what the EKF does (via the ``fuse_imu`` arg):

* ``fuse_imu:=false`` (default) -> ``ekf_rf2o_only.yaml``: rf2o is the ONLY input.
  The EKF is a pure high-rate TF republisher -- rf2o's ~10 Hz, scan-time-stamped
  estimate is too stale for nav2's "now" TF lookups, so the EKF's 30 Hz
  predict-to-current-time timer keeps ``odom -> base_footprint`` fresh.
* ``fuse_imu:=true`` -> ``ekf.yaml``: also fuses the gyro (/imu/data_raw) yaw rate
  for drift-resistant heading during fast in-place rotations.

Both files carry unprefixed frames (map/odom/base_link/world); this helper rewrites
them to the ``<robot_name>/`` prefix (matching robot_state_publisher and the rest of
waveshare.launch.xml), mirroring slam.launch.py.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from nav2_common.launch import RewrittenYaml


def _launch_setup(context, *args, **kwargs):
    robot_name = LaunchConfiguration('robot_name').perform(context)
    use_sim_time = LaunchConfiguration('use_sim_time').perform(context)
    fuse_imu = LaunchConfiguration('fuse_imu').perform(context).lower() in ('true', '1')
    ekf_params_file = LaunchConfiguration('ekf_params_file').perform(context)

    # No explicit params override -> pick the file from fuse_imu: the full rf2o+IMU
    # fusion (ekf.yaml) or the rf2o-only high-rate TF republisher (ekf_rf2o_only.yaml).
    if not ekf_params_file:
        share = get_package_share_directory('waveshare_launch')
        ekf_params_file = os.path.join(
            share, 'config', 'ekf.yaml' if fuse_imu else 'ekf_rf2o_only.yaml')

    # Empty robot_name -> no prefix (single-robot behaviour preserved).
    prefix = f'{robot_name}/' if robot_name else ''

    rewritten_params = RewrittenYaml(
        source_file=ekf_params_file,
        param_rewrites={
            'map_frame': f'{prefix}map',
            'odom_frame': f'{prefix}odom',
            'world_frame': f'{prefix}odom',
            'base_link_frame': f'{prefix}base_footprint',
        },
        convert_types=True,
    )

    use_sim_time_bool = use_sim_time.lower() in ('true', '1')

    return [
        # rf2o publishes all-zero covariances, which the EKF over-trusts, and its
        # twist in the LASER frame (sign-inverted here: lidar mounted yaw=pi). This
        # relay stamps sane covariances and rotates the twist into the base frame,
        # /odom_rf2o -> /odom_rf2o_cov (the EKF's odom0).
        Node(
            package='waveshare_launch',
            executable='rf2o_covariance_relay.py',
            name='rf2o_covariance_relay',
            output='screen',
            parameters=[{
                'use_sim_time': use_sim_time_bool,
                'twist_frame_yaw': float(LaunchConfiguration('lidar_yaw').perform(context)),
            }],
        ),
        Node(
            package='robot_localization',
            executable='ekf_node',
            name='ekf_filter_node',
            output='screen',
            parameters=[rewritten_params, {'use_sim_time': use_sim_time_bool}],
            # rf2o's raw odometry stays on /odom_rf2o; the fused estimate becomes the
            # canonical /odom that nav2 and the LiveKit bridge consume.
            remappings=[('odometry/filtered', '/odom')],
        ),
    ]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('robot_name', default_value='robot_1',
                              description='TF frame prefix for the EKF map/odom/base frames.'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('fuse_imu', default_value='false',
                              description='true -> fuse the gyro (ekf.yaml); '
                                          'false -> rf2o-only high-rate TF republisher '
                                          '(ekf_rf2o_only.yaml).'),
        DeclareLaunchArgument('ekf_params_file', default_value='',
                              description='Override EKF params file; empty picks one from '
                                          'fuse_imu. Frames rewritten per robot_name.'),
        DeclareLaunchArgument('lidar_yaw', default_value='0.0',
                              description="Laser yaw in the base frame (rad); rotates rf2o's "
                                          'laser-frame twist into the base frame in the relay. '
                                          "Keep equal to the parent launch's lidar_yaw."),
        OpaqueFunction(function=_launch_setup),
    ])
