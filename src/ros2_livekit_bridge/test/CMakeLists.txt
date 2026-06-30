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

if(NOT BUILD_TESTING)
  return()
endif()

find_package(ament_cmake_gtest REQUIRED)
find_package(ament_cmake_pytest REQUIRED)
find_package(ament_lint_auto REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(test_msgs REQUIRED)

ament_add_pytest_test(livekit_bridge_local_launch_test
  launch/test_livekit_bridge_local_launch.py
)

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
file(GLOB UNIT_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/introspection/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/ros2_cli/*.cpp"
)
list(SORT UNIT_TEST_SOURCES)

if(UNIT_TEST_SOURCES)
  ament_auto_add_gtest(ros2_livekit_bridge_unit_tests
    SKIP_LINKING_MAIN_LIBRARIES
    ${UNIT_TEST_SOURCES}
  )
endif()

if(TARGET ros2_livekit_bridge_unit_tests)
  target_compile_definitions(ros2_livekit_bridge_unit_tests PRIVATE BUILD_TESTING=1)
  target_link_libraries(ros2_livekit_bridge_unit_tests
    ros2_livekit_bridge_lib
  )
  ament_target_dependencies(ros2_livekit_bridge_unit_tests nav_msgs std_srvs test_msgs)
  livekit_set_runtime_rpath(ros2_livekit_bridge_unit_tests)
endif()

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------
file(GLOB INTEGRATION_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/*_e2e.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/*_test.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/ros2_cli/*_e2e.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/ros2_cli/*_test.cpp"
)
list(SORT INTEGRATION_TEST_SOURCES)

if(INTEGRATION_TEST_SOURCES)
  ament_auto_add_gtest(ros2_livekit_bridge_integration_tests
    ${INTEGRATION_TEST_SOURCES}
  )
endif()

if(TARGET ros2_livekit_bridge_integration_tests)
  target_include_directories(ros2_livekit_bridge_integration_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
  )
  ament_target_dependencies(ros2_livekit_bridge_integration_tests std_srvs)
  target_link_libraries(ros2_livekit_bridge_integration_tests
    ros2_livekit_bridge_lib
  )
  livekit_set_runtime_rpath(ros2_livekit_bridge_integration_tests)
  set_tests_properties(ros2_livekit_bridge_integration_tests PROPERTIES
    TIMEOUT 240
  )
endif()

# ---------------------------------------------------------------------------
# Standalone integration executable
# ---------------------------------------------------------------------------
ament_auto_add_executable(livekit_connect_node
  integration/livekit_connect_node.cpp
)
target_link_libraries(livekit_connect_node
  "${LIVEKIT_CORE_TARGET}"
  ${LIVEKIT_EXTRA_LINK_LIBS}
)
livekit_set_runtime_rpath(livekit_connect_node)

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------
set(ament_cmake_copyright_FOUND TRUE)
set(ament_cmake_cpplint_FOUND TRUE)
list(APPEND AMENT_LINT_AUTO_EXCLUDE ament_cmake_uncrustify)
set(ament_cmake_clang_format_CONFIG_FILE "${CMAKE_SOURCE_DIR}/../../.clang-format")
ament_lint_auto_find_test_dependencies()
