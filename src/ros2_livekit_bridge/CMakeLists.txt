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
project(ros2_livekit_bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake_auto REQUIRED)
ament_auto_find_build_dependencies(REQUIRED
  ament_index_cpp
  diagnostic_msgs
  diagnostic_updater
  geometry_msgs
  rclcpp
  ros2_medkit_serialization
  rosbag2_cpp
  rosbag2_storage
  ros2_livekit_bridge_config
  ros2_livekit_bridge_msgs
  rosidl_typesupport_cpp
  rosidl_typesupport_introspection_cpp
  sensor_msgs
  std_msgs
  yaml_cpp_vendor)

# Use rosbag2's renderer where its header is available.
find_file(LIVEKIT_ROSBAG2_MESSAGE_DEFINITIONS_HEADER
  NAMES rosbag2_cpp/message_definitions/local_message_definition_source.hpp
  PATHS ${rosbag2_cpp_INCLUDE_DIRS}
  NO_DEFAULT_PATH)
if(LIVEKIT_ROSBAG2_MESSAGE_DEFINITIONS_HEADER)
  set(LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS 1)
  message(STATUS "rosbag2 definitions   : yes (${LIVEKIT_ROSBAG2_MESSAGE_DEFINITIONS_HEADER})")
else()
  set(LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS 0)
  message(STATUS "rosbag2 definitions   : no (using the bundled definition source)")
endif()

set(_ros_distro "$ENV{ROS_DISTRO}")
if(_ros_distro)
  string(TOUPPER "${_ros_distro}" _ros_distro_upper)
  string(REGEX REPLACE "[^A-Z0-9_]" "_" _ros_distro_upper
    "${_ros_distro_upper}")
  add_compile_definitions("ROS_DISTRO_${_ros_distro_upper}=1")
  message(STATUS "ROS distribution      : ${_ros_distro} (ROS_DISTRO_${_ros_distro_upper})")
else()
  message(WARNING
    "ROS_DISTRO is not set; no ROS_DISTRO_<NAME> compatibility macro will be defined")
endif()

# ---------------------------------------------------------------------------
# LiveKit SDK install
#
# Build the pinned vcstool checkout with LIVEKIT_BUILD_SDK_FROM_SOURCE, use a local
# install prefix via LIVEKIT_LOCAL_SDK_DIR, or download a release asset from
# GitHub. The same version pin is mirrored in this package's colcon.pkg.
#
# Examples:
#   colcon build --packages-select ros2_livekit_bridge
#
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DLIVEKIT_BUILD_SDK_FROM_SOURCE=ON
#
#   export LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk
#   colcon build --packages-select ros2_livekit_bridge
#
#   # Override the pin for a one-off build (or track upstream with "latest"):
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DLIVEKIT_SDK_VERSION=latest
#
# ---------------------------------------------------------------------------
set(LIVEKIT_SDK_VERSION "1.5.0" CACHE STRING
  "LiveKit C++ SDK version to download (use \"latest\" to resolve via GitHub API)")
include(ConfigureLiveKitSDK)
livekit_configure_sdk(
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../externals/client-sdk-cpp"
  BUILD_ROOT "${CMAKE_BINARY_DIR}/_deps"
)

find_package(LiveKit CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(ros2_medkit_cmake REQUIRED)
include(ROS2MedkitCompat)
medkit_find_yaml_cpp()

if(TARGET LiveKit::livekit)
  set(LIVEKIT_CORE_TARGET LiveKit::livekit)
elseif(TARGET livekit)
  set(LIVEKIT_CORE_TARGET livekit)
else()
  message(FATAL_ERROR
    "Could not find a LiveKit core target (expected LiveKit::livekit or livekit).")
endif()

set(LIVEKIT_RUNTIME_LIBRARY_DIR "$<TARGET_FILE_DIR:${LIVEKIT_CORE_TARGET}>")

# Point a target's RPATH at the LiveKit SDK so its shared library is resolved at
# both build and install time. Shared by the targets here and under test/.
function(livekit_set_runtime_rpath target)
  set_target_properties(${target} PROPERTIES BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}")
  if(APPLE)
    set_target_properties(${target} PROPERTIES INSTALL_RPATH "@loader_path;@loader_path/..")
  elseif(UNIX)
    set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/..")
  endif()
endfunction()

if(LIVEKIT_LOCAL_SDK_DIR)
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _livekit_imported_config)
    get_target_property(_livekit_imported_location "${LIVEKIT_CORE_TARGET}"
      "IMPORTED_LOCATION_${_livekit_imported_config}")
  endif()

  if(NOT _livekit_imported_location)
    get_target_property(_livekit_imported_location "${LIVEKIT_CORE_TARGET}"
      IMPORTED_LOCATION_RELEASE)
  endif()
  if(NOT _livekit_imported_location)
    get_target_property(_livekit_imported_location "${LIVEKIT_CORE_TARGET}"
      IMPORTED_LOCATION_RELWITHDEBINFO)
  endif()
  if(NOT _livekit_imported_location)
    get_target_property(_livekit_imported_location "${LIVEKIT_CORE_TARGET}"
      IMPORTED_LOCATION_DEBUG)
  endif()
  if(NOT _livekit_imported_location)
    get_target_property(_livekit_imported_location "${LIVEKIT_CORE_TARGET}"
      IMPORTED_LOCATION)
  endif()

  if(_livekit_imported_location)
    if(APPLE AND NOT _livekit_imported_location MATCHES "\\.(dylib|a)$")
      message(FATAL_ERROR
        "LiveKit SDK at ${LIVEKIT_LOCAL_SDK_DIR} is not a macOS build.\n"
        "Imported library: ${_livekit_imported_location}\n"
        "Build or install a macOS SDK bundle for this workspace.")
    elseif(UNIX AND NOT APPLE AND NOT _livekit_imported_location MATCHES "\\.(so([.].*)?|a)$")
      message(FATAL_ERROR
        "LiveKit SDK at ${LIVEKIT_LOCAL_SDK_DIR} is not a Linux build.\n"
        "Imported library: ${_livekit_imported_location}\n"
        "This usually means a macOS `.dylib` SDK install was mounted into a Linux container.\n"
        "Build or install a Linux SDK bundle for this workspace, then rerun colcon.")
    elseif(WIN32 AND NOT _livekit_imported_location MATCHES "\\.(dll|lib)$")
      message(FATAL_ERROR
        "LiveKit SDK at ${LIVEKIT_LOCAL_SDK_DIR} is not a Windows build.\n"
        "Imported library: ${_livekit_imported_location}\n"
        "Build or install a Windows SDK bundle for this workspace.")
    endif()
  endif()
endif()

set(LIVEKIT_EXTRA_LINK_LIBS)
if(TARGET spdlog::spdlog)
  list(APPEND LIVEKIT_EXTRA_LINK_LIBS spdlog::spdlog)
endif()

# Some ROS/Jazzy toolchains still require libfmt to appear explicitly on the
# final link line when headers from spdlog/LiveKit are used in this target.
find_package(fmt CONFIG QUIET)
if(TARGET fmt::fmt)
  list(APPEND LIVEKIT_EXTRA_LINK_LIBS fmt::fmt)
elseif(TARGET fmt::fmt-header-only)
  list(APPEND LIVEKIT_EXTRA_LINK_LIBS fmt::fmt-header-only)
else()
  find_library(FMT_LIBRARY NAMES fmt)
  if(FMT_LIBRARY)
    list(APPEND LIVEKIT_EXTRA_LINK_LIBS "${FMT_LIBRARY}")
  endif()
endif()

message(STATUS "LiveKit core target   : ${LIVEKIT_CORE_TARGET}")
message(STATUS "LiveKit SDK prefix    : ${LIVEKIT_LOCAL_SDK_DIR}")

# ---------------------------------------------------------------------------
# Introspection library
# ---------------------------------------------------------------------------
ament_auto_add_library(introspection_lib SHARED
  src/introspection/introspection_utils.cpp
  src/introspection/runtime_type_support.cpp
)
target_link_libraries(introspection_lib
  nlohmann_json::nlohmann_json
  yaml-cpp::yaml-cpp
)
medkit_target_dependencies(introspection_lib
  ros2_medkit_serialization
)

# ---------------------------------------------------------------------------
# ROS2 CLI library
# ---------------------------------------------------------------------------
ament_auto_add_library(ros2_cli_lib SHARED
  src/cli/interface_show.cpp
  src/cli/json_converters.cpp
  src/cli/service_call.cpp
  src/cli/service_list.cpp
  src/cli/topic_list.cpp
  src/cli/topic_pub.cpp
  src/cli/utils.cpp
  src/cli/manager.cpp
)
target_link_libraries(ros2_cli_lib
  introspection_lib
  "${LIVEKIT_CORE_TARGET}"
  nlohmann_json::nlohmann_json
  ${LIVEKIT_EXTRA_LINK_LIBS}
)
livekit_set_runtime_rpath(ros2_cli_lib)

# ---------------------------------------------------------------------------
# Bridge library
# ---------------------------------------------------------------------------
ament_auto_add_library(ros2_livekit_bridge_lib SHARED
  src/diagnostics/connection_health.cpp
  src/latched_topic_forwarder.cpp
  src/ros2_livekit_bridge.cpp
  src/schema/manager.cpp
  src/schema/renderer.cpp
  src/service_forwarder.cpp
  src/topic_forwarder.cpp
  src/utils/base64.cpp
  src/utils/config_mapping.cpp
  src/utils/image_conversion.cpp
  src/utils/ros_utils.cpp
  src/utils/topic_matcher.cpp
)
target_compile_definitions(ros2_livekit_bridge_lib PRIVATE
  LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS=${LIVEKIT_HAS_ROSBAG2_MESSAGE_DEFINITIONS}
)
target_link_libraries(ros2_livekit_bridge_lib
  introspection_lib
  ros2_cli_lib
  "${LIVEKIT_CORE_TARGET}"
  nlohmann_json::nlohmann_json
  OpenSSL::Crypto
  ${LIVEKIT_EXTRA_LINK_LIBS}
)
livekit_set_runtime_rpath(ros2_livekit_bridge_lib)

# ---------------------------------------------------------------------------
# Main bridge node
# ---------------------------------------------------------------------------
ament_auto_add_executable(ros2_livekit_bridge_node
  src/ros2_livekit_bridge_node.cpp
)
target_link_libraries(ros2_livekit_bridge_node
  ros2_livekit_bridge_lib
)
livekit_set_runtime_rpath(ros2_livekit_bridge_node)

if(LIVEKIT_LOCAL_SDK_DIR)
  set(LIVEKIT_SDK_INSTALL_ROOT "${LIVEKIT_LOCAL_SDK_DIR}")
else()
  set(LIVEKIT_SDK_INSTALL_ROOT "${LIVEKIT_SDK_EXTRACTED_ROOT}")
endif()

if(APPLE)
  install(DIRECTORY "${LIVEKIT_SDK_INSTALL_ROOT}/lib/"
    DESTINATION lib
    FILES_MATCHING PATTERN "*.dylib")
elseif(UNIX)
  install(DIRECTORY "${LIVEKIT_SDK_INSTALL_ROOT}/lib/"
    DESTINATION lib
    FILES_MATCHING PATTERN "*.so" PATTERN "*.so.*")
elseif(WIN32)
  install(DIRECTORY "${LIVEKIT_SDK_INSTALL_ROOT}/bin/"
    DESTINATION bin
    FILES_MATCHING PATTERN "*.dll")
endif()

if(BUILD_TESTING)
  target_compile_definitions(ros2_cli_lib PRIVATE BUILD_TESTING=1)
  add_subdirectory(test)
endif()

ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR INSTALL_TO_SHARE config launch)
