#
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
project(test_utilities)

find_package(ament_cmake_auto REQUIRED)

# Define the imported targets that the transitively-pulled medkit and ROS Portal
# CMake configs reference (yaml-cpp::yaml-cpp, nlohmann_json::nlohmann_json)
# before ament_auto_find_build_dependencies resolves those packages.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(ros2_medkit_cmake REQUIRED)
include(ROS2MedkitCompat)
medkit_find_yaml_cpp()

ament_auto_find_build_dependencies()

install(PROGRAMS
  scripts/depth_time_offset_node.py
  scripts/latency_probe_publisher.py
  scripts/latency_probe_subscriber.py
  DESTINATION lib/${PROJECT_NAME}
)

# Reuses ROS Portal introspection runtime (RuntimeMessageTypeSupport,
# DynamicMessage) and the medkit JsonSerializer to print a type's JSON.
ament_auto_add_executable(message_to_json
  src/message_to_json.cpp
)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
  USE_SOURCE_PERMISSIONS
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)

  ament_lint_auto_find_test_dependencies()
endif()

ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR)
