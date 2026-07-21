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

cmake_minimum_required(VERSION 3.20)
project(ros_portal_msgs)

find_package(ament_cmake_auto REQUIRED)
find_package(rosidl_default_generators REQUIRED)

ament_auto_find_build_dependencies()

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/LatencyTimestamps.msg"
  "msg/LatencyMetric.msg"
  "msg/LatencyStats.msg"
  "srv/Ros2InterfaceShow.srv"
  "srv/Ros2ServiceCall.srv"
  "srv/Ros2ServiceList.srv"
  "srv/Ros2TopicList.srv"
  "srv/Ros2TopicPub.srv"
  DEPENDENCIES builtin_interfaces std_msgs
)

ament_auto_package()
