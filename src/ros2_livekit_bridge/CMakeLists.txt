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

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(ros2_foxglove_adapters REQUIRED)

# ---------------------------------------------------------------------------
# LiveKit SDK install
#
# Use a local install prefix via LIVEKIT_LOCAL_SDK_DIR, or download a release
# asset from GitHub. By default, the latest SDK release is used.
#
# Examples:
#   colcon build --packages-select ros2_livekit_bridge
#
#   export LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk
#   colcon build --packages-select ros2_livekit_bridge
#
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DLIVEKIT_SDK_VERSION=1.2.3
#
# If you build the SDK locally for use here, build the SDK itself with
# -DLIVEKIT_USE_SYSTEM_SPDLOG=ON. Do not pass that option to colcon; it only
# affects the SDK build and avoids loading multiple spdlog/fmt copies.
# ---------------------------------------------------------------------------
set(LIVEKIT_SDK_VERSION "latest" CACHE STRING
  "LiveKit C++ SDK version to download")
set(LIVEKIT_SDK_DIR "$ENV{LIVEKIT_SDK_DIR}" CACHE PATH
  "Deprecated alias for LIVEKIT_LOCAL_SDK_DIR. Path to a local LiveKit SDK install prefix.")
set(LIVEKIT_LOCAL_SDK_DIR "$ENV{LIVEKIT_LOCAL_SDK_DIR}" CACHE PATH
  "Path to a local LiveKit SDK install prefix (skips download). Defaults to \$ENV{LIVEKIT_LOCAL_SDK_DIR}.")

if(LIVEKIT_SDK_DIR AND NOT LIVEKIT_LOCAL_SDK_DIR)
  message(STATUS
    "LIVEKIT_SDK_DIR is deprecated; treating it as LIVEKIT_LOCAL_SDK_DIR")
  set(LIVEKIT_LOCAL_SDK_DIR "${LIVEKIT_SDK_DIR}" CACHE PATH
    "Path to a local LiveKit SDK install prefix (skips download). Defaults to \$ENV{LIVEKIT_LOCAL_SDK_DIR}."
    FORCE)
endif()

if(LIVEKIT_LOCAL_SDK_DIR)
  if(NOT EXISTS "${LIVEKIT_LOCAL_SDK_DIR}")
    message(FATAL_ERROR
      "LIVEKIT_LOCAL_SDK_DIR does not exist: ${LIVEKIT_LOCAL_SDK_DIR}")
  endif()
  message(STATUS "Using local LiveKit SDK: ${LIVEKIT_LOCAL_SDK_DIR}")
  list(PREPEND CMAKE_PREFIX_PATH "${LIVEKIT_LOCAL_SDK_DIR}")
else()
  include(LiveKitSDK)
  livekit_sdk_setup(
    VERSION "${LIVEKIT_SDK_VERSION}"
    SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
    GITHUB_TOKEN "$ENV{GITHUB_TOKEN}"
  )
endif()

find_package(LiveKit CONFIG REQUIRED)

if(TARGET LiveKit::livekit)
  set(LIVEKIT_CORE_TARGET LiveKit::livekit)
elseif(TARGET livekit)
  set(LIVEKIT_CORE_TARGET livekit)
else()
  message(FATAL_ERROR
    "Could not find a LiveKit core target (expected LiveKit::livekit or livekit).")
endif()

set(LIVEKIT_RUNTIME_LIBRARY_DIR "$<TARGET_FILE_DIR:${LIVEKIT_CORE_TARGET}>")

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
if(LIVEKIT_LOCAL_SDK_DIR)
  message(STATUS "LiveKit SDK prefix    : ${LIVEKIT_LOCAL_SDK_DIR}")
else()
  message(STATUS "LiveKit SDK version   : ${LIVEKIT_SDK_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Main bridge node
# ---------------------------------------------------------------------------
add_executable(ros2_livekit_bridge_node
  src/ros2_livekit_bridge_node.cpp
  src/ros2_livekit_bridge.cpp
)
target_include_directories(ros2_livekit_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(ros2_livekit_bridge_node
  "${LIVEKIT_CORE_TARGET}"
  ros2_foxglove_adapters::ros2_foxglove_adapters
  ${LIVEKIT_EXTRA_LINK_LIBS}
)
set_target_properties(ros2_livekit_bridge_node PROPERTIES
  BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
  INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
)
ament_target_dependencies(ros2_livekit_bridge_node
  rclcpp sensor_msgs std_msgs geometry_msgs nav_msgs ros2_foxglove_adapters)

install(TARGETS ros2_livekit_bridge_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

# ---------------------------------------------------------------------------
# Integration test node
#
# Enable with:
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DBUILD_INTEGRATION_TESTS=ON
# ---------------------------------------------------------------------------
option(BUILD_INTEGRATION_TESTS
  "Build LiveKit library integration tests" OFF)

if(BUILD_INTEGRATION_TESTS)
  add_executable(livekit_connect_node
    test/integration/livekit_connect_node.cpp
  )
  target_link_libraries(livekit_connect_node
    "${LIVEKIT_CORE_TARGET}"
    ${LIVEKIT_EXTRA_LINK_LIBS}
  )
  set_target_properties(livekit_connect_node PROPERTIES
    BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
    INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
  )
  ament_target_dependencies(livekit_connect_node rclcpp)

  install(TARGETS livekit_connect_node
    DESTINATION lib/${PROJECT_NAME})
endif()

ament_package()
