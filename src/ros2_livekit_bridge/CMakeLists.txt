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
  geometry_msgs
  nav_msgs
  rclcpp
  sensor_msgs
  std_msgs
  yaml_cpp_vendor)

find_package(yaml-cpp REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import jinja2"
  RESULT_VARIABLE _jinja2_result
)
if(NOT _jinja2_result EQUAL 0)
  message(FATAL_ERROR
    "Python module jinja2 is required to generate config_parser.hpp/cpp. "
    "Install python3-jinja2.")
endif()

set(CONFIG_SCHEMA
  "${CMAKE_CURRENT_SOURCE_DIR}/schema/ros_livekit_bridge_config.schema.json")
set(CONFIG_PARSER_TEMPLATES
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/templates/config_parser.hpp.j2"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/templates/config_parser.cpp.j2")
set(CONFIG_PARSER_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/config_parser")
set(CONFIG_PARSER_GENERATED_INCLUDE_DIR
  "${CONFIG_PARSER_GENERATED_DIR}/include")
set(CONFIG_PARSER_GENERATED_HEADER
  "${CONFIG_PARSER_GENERATED_INCLUDE_DIR}/ros2_livekit_bridge/config/config_parser.hpp")
set(CONFIG_PARSER_GENERATED_SOURCE
  "${CONFIG_PARSER_GENERATED_DIR}/src/config/config_parser.cpp")

add_custom_command(
  OUTPUT
    "${CONFIG_PARSER_GENERATED_HEADER}"
    "${CONFIG_PARSER_GENERATED_SOURCE}"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_config_parser.py"
    --schema "${CONFIG_SCHEMA}"
    --header "${CONFIG_PARSER_GENERATED_HEADER}"
    --source "${CONFIG_PARSER_GENERATED_SOURCE}"
  DEPENDS
    "${CONFIG_SCHEMA}"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_config_parser.py"
    ${CONFIG_PARSER_TEMPLATES}
  COMMENT "Generating config parser from JSON schema"
  VERBATIM
)

add_custom_target(generate_config_parser ALL
  DEPENDS
    "${CONFIG_PARSER_GENERATED_HEADER}"
    "${CONFIG_PARSER_GENERATED_SOURCE}"
)

set_source_files_properties(
  "${CONFIG_PARSER_GENERATED_HEADER}"
  "${CONFIG_PARSER_GENERATED_SOURCE}"
  PROPERTIES GENERATED TRUE)

# ---------------------------------------------------------------------------
# LiveKit SDK install
#
# Use a local install prefix via LIVEKIT_LOCAL_SDK_DIR, or download a release
# asset from GitHub. By default, a pinned SDK version is downloaded (see
# LIVEKIT_SDK_VERSION below). The same pin is mirrored in this package's
# colcon.pkg so that a bare `colcon build` is reproducible without the pin
# leaking into other packages' CMake invocations.
#
# Examples:
#   colcon build --packages-select ros2_livekit_bridge
#
#   export LIVEKIT_LOCAL_SDK_DIR=/path/to/livekit-sdk
#   colcon build --packages-select ros2_livekit_bridge
#
#   # Override the pin for a one-off build (or track upstream with "latest"):
#   colcon build --packages-select ros2_livekit_bridge \
#     --cmake-args -DLIVEKIT_SDK_VERSION=latest
#
# If you build the SDK locally for use here, build the SDK itself with
# -DLIVEKIT_USE_SYSTEM_SPDLOG=ON. Do not pass that option to colcon; it only
# affects the SDK build and avoids loading multiple spdlog/fmt copies.
# ---------------------------------------------------------------------------
set(LIVEKIT_SDK_VERSION "1.0.0" CACHE STRING
  "LiveKit C++ SDK version to download (use \"latest\" to resolve via GitHub API)")
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
ament_auto_add_executable(ros2_livekit_bridge_node
  src/ros2_livekit_bridge_node.cpp
  src/ros2_livekit_bridge.cpp
  src/utils/image_conversion.cpp
  src/utils/topic_matcher.cpp
)
target_link_libraries(ros2_livekit_bridge_node
  "${LIVEKIT_CORE_TARGET}"
  ${LIVEKIT_EXTRA_LINK_LIBS}
)
set_target_properties(ros2_livekit_bridge_node PROPERTIES
  BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
  INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
)
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_auto_add_gtest(livekit_connect_node_test
    test/integration/livekit_connect_node.cpp
  )
  if(TARGET livekit_connect_node_test)
    target_compile_definitions(livekit_connect_node_test PRIVATE
      LIVEKIT_CONNECT_NODE_TEST
    )
    target_link_libraries(livekit_connect_node_test
      "${LIVEKIT_CORE_TARGET}"
      ${LIVEKIT_EXTRA_LINK_LIBS}
    )
    set_target_properties(livekit_connect_node_test PROPERTIES
      BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
      INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
    )
  endif()

  ament_auto_add_gtest(topic_matcher_test
    test/unit/topic_matcher_test.cpp
    src/utils/topic_matcher.cpp
  )

  ament_auto_add_gtest(image_conversion_test
    test/unit/image_conversion_test.cpp
    src/utils/image_conversion.cpp
  )

  ament_auto_add_gtest(config_parser_test
    test/unit/config_parser_test.cpp
    "${CONFIG_PARSER_GENERATED_SOURCE}"
    src/config/utils.cpp
  )
  if(TARGET config_parser_test)
    add_dependencies(config_parser_test generate_config_parser)
    target_compile_definitions(config_parser_test PRIVATE
      ROS2_LIVEKIT_BRIDGE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    target_include_directories(config_parser_test PRIVATE
      "${CONFIG_PARSER_GENERATED_INCLUDE_DIR}"
      src
    )
    target_link_libraries(config_parser_test
      yaml-cpp::yaml-cpp
    )
  endif()

  ament_auto_add_gtest(config_utils_test
    test/config/test_utils.cpp
    src/config/utils.cpp
  )
  if(TARGET config_utils_test)
    target_include_directories(config_utils_test PRIVATE src)
    target_link_libraries(config_utils_test
      yaml-cpp::yaml-cpp
    )
  endif()

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

install(
  DIRECTORY "${CONFIG_PARSER_GENERATED_INCLUDE_DIR}/"
  DESTINATION "include/${PROJECT_NAME}"
)

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
  ament_auto_add_executable(livekit_connect_node
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
endif()

ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR INSTALL_TO_SHARE config launch schema)
