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

# ScaRF-SLAM demo, SLAM host side. See README.md.
#
# Receives the SBS stereo stream through ROS Portal, rectifies it, runs
# RTAB-Map stereo odometry and SLAM, and records the ScaRF-SLAM input bag to
# <output_dir>/slam_bag. Run ScaRF-SLAM on that bag after the run.

from datetime import datetime
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.actions import LogInfo
from launch.actions import OpaqueFunction
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare

PACKAGE = 'livekit_scarf_slam'
RECT_NAMESPACE = '/stereo'


def _output_dir(context):
    value = LaunchConfiguration('output_dir').perform(context).strip()
    if value:
        return Path(value).expanduser()
    return Path.home() / 'scarf_slam_runs' / datetime.now().strftime('%Y%m%d_%H%M%S')


def _launch_setup(context):
    output_dir = _output_dir(context)
    if (output_dir / 'slam_bag').exists():
        raise RuntimeError(f'{output_dir / "slam_bag"} already exists. Pick a new output_dir.')
    output_dir.mkdir(parents=True, exist_ok=True)

    calibration_path = LaunchConfiguration('calibration_path')
    sbs_topic = LaunchConfiguration('sbs_topic')
    rtabmap_params = PathJoinSubstitution([FindPackageShare(PACKAGE), 'config', 'rtabmap',
                                           'stereo.yaml'])
    rtabmap_remappings = [
        (f'{side}/{name}', f'{RECT_NAMESPACE}/{side}/{name}')
        for side in ('left', 'right') for name in ('image_rect', 'camera_info')]

    return [
        LogInfo(msg=f'Writing the run output to {output_dir}'),
        Node(
            package=PACKAGE,
            executable='stereo_rectify_node',
            name='stereo_rectify',
            output='screen',
            parameters=[{
                'calibration_path': calibration_path,
                'input_topic': sbs_topic,
                'output_namespace': RECT_NAMESPACE,
                'decode_reduction': ParameterValue(
                    LaunchConfiguration('decode_reduction'), value_type=int),
                'rect_scale': ParameterValue(LaunchConfiguration('rect_scale'), value_type=float),
            }],
        ),
        Node(
            package='rtabmap_odom',
            executable='stereo_odometry',
            namespace='rtabmap',
            output='screen',
            parameters=[rtabmap_params],
            remappings=rtabmap_remappings,
        ),
        Node(
            package='rtabmap_slam',
            executable='rtabmap',
            namespace='rtabmap',
            output='screen',
            parameters=[rtabmap_params, {'database_path': str(output_dir / 'rtabmap.db')}],
            remappings=rtabmap_remappings,
            arguments=['-d'],
        ),
        Node(
            package='rtabmap_viz',
            executable='rtabmap_viz',
            namespace='rtabmap',
            output='screen',
            parameters=[rtabmap_params],
            remappings=rtabmap_remappings,
            condition=IfCondition(LaunchConfiguration('rtabmap_viz')),
        ),
        Node(
            package=PACKAGE,
            executable='slam_bag_recorder_node',
            name='slam_bag_recorder',
            output='screen',
            parameters=[{
                'calibration_path': calibration_path,
                'sbs_topic': sbs_topic,
                'map_data_topic': '/rtabmap/mapData',
                'output_path': str(output_dir / 'slam_bag'),
            }],
        ),
    ]


def generate_launch_description():
    ros_portal_launch = PathJoinSubstitution([
        FindPackageShare('ros_portal'), 'launch', 'ros_portal_local.launch.py'])
    ros_portal_config = PathJoinSubstitution([
        FindPackageShare(PACKAGE), 'config', 'ros_portal', 'slam_host.yaml'])

    return LaunchDescription([
        DeclareLaunchArgument(
            'calibration_path',
            default_value='/livekit_ws/stereo-calibration/stereo_calibration.json',
            description='stereo_calibration.json of the SBS fisheye camera.'),
        DeclareLaunchArgument(
            'sbs_topic', default_value='/stereo/image_raw/compressed',
            description='SBS sensor_msgs/CompressedImage topic.'),
        DeclareLaunchArgument(
            'output_dir', default_value='',
            description='Run output folder. Default: ~/scarf_slam_runs/<date>_<time>.'),
        DeclareLaunchArgument(
            'decode_reduction', default_value='2',
            description='Decode the JPEG at 1/n size: 1, 2, 4, or 8.'),
        DeclareLaunchArgument(
            'rect_scale', default_value='0.5',
            description='Size of the rectified images relative to the calibration.'),
        DeclareLaunchArgument(
            'use_ros_portal', default_value='true',
            description='Start ROS Portal. Set false to play a recorded SBS bag.'),
        DeclareLaunchArgument('room_name', default_value='robo_room'),
        DeclareLaunchArgument('identity', default_value='slam_host'),
        DeclareLaunchArgument('livekit_url', default_value='ws://host.docker.internal:7880'),
        DeclareLaunchArgument(
            'rtabmap_viz', default_value='false', description='Start the RTAB-Map GUI.'),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(ros_portal_launch),
            launch_arguments={
                'config_path': ros_portal_config,
                'identity': LaunchConfiguration('identity'),
                'room_name': LaunchConfiguration('room_name'),
                'livekit_url': LaunchConfiguration('livekit_url'),
            }.items(),
            condition=IfCondition(LaunchConfiguration('use_ros_portal')),
        ),
        OpaqueFunction(function=_launch_setup),
    ])
