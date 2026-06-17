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

# ============================================================================
# Unit Tests
# ============================================================================

if(BUILD_TESTING)
  file(GLOB UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
  )
  list(SORT UNIT_TEST_SOURCES)

  if(UNIT_TEST_SOURCES)
    ament_auto_add_gtest(ros2_livekit_bridge_unit_tests
      ${UNIT_TEST_SOURCES}
    )
  endif()

  if(TARGET ros2_livekit_bridge_unit_tests)
    target_link_libraries(ros2_livekit_bridge_unit_tests
      ros2_livekit_bridge_lib
    )
    set_target_properties(ros2_livekit_bridge_unit_tests PROPERTIES
      BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
      INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
    )
  endif()
endif()

# ============================================================================
# Integration Tests
# ============================================================================

if(BUILD_TESTING)
  file(GLOB INTEGRATION_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/*_test.cpp"
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
    target_link_libraries(ros2_livekit_bridge_integration_tests
      ros2_livekit_bridge_lib
    )
    set_target_properties(ros2_livekit_bridge_integration_tests PROPERTIES
      BUILD_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
      INSTALL_RPATH "${LIVEKIT_RUNTIME_LIBRARY_DIR}"
    )
    set_tests_properties(ros2_livekit_bridge_integration_tests PROPERTIES
      TIMEOUT 240
    )
  endif()
endif()

# ============================================================================
# Standalone Integration Executable
# ============================================================================

if(BUILD_TESTING)
  ament_auto_add_executable(livekit_connect_node
    integration/livekit_connect_node.cpp
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
