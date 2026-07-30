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
project(ros2_livekit_bridge_config)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake_auto REQUIRED)
ament_auto_find_build_dependencies(REQUIRED
  yaml_cpp_vendor)

find_package(ros2_medkit_cmake REQUIRED)
include(ROS2MedkitCompat)
medkit_find_yaml_cpp()
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
  "${CMAKE_CURRENT_SOURCE_DIR}/schema/ros2_livekit_bridge_config.schema.json")
set(CONFIG_PARSER_TEMPLATES
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/templates/config_parser.hpp.j2"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/templates/config_parser.cpp.j2")
set(CONFIG_PARSER_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated/config_parser")
set(CONFIG_PARSER_GENERATED_INCLUDE_DIR
  "${CONFIG_PARSER_GENERATED_DIR}/include")
set(CONFIG_PARSER_GENERATED_HEADER
  "${CONFIG_PARSER_GENERATED_INCLUDE_DIR}/ros2_livekit_bridge_config/config/config_parser.hpp")
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

ament_auto_add_library(${PROJECT_NAME} SHARED
  "${CONFIG_PARSER_GENERATED_SOURCE}"
  src/config/utils.cpp
)
add_dependencies(${PROJECT_NAME} generate_config_parser)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CONFIG_PARSER_GENERATED_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    src
)
target_link_libraries(${PROJECT_NAME}
  yaml-cpp::yaml-cpp
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_auto_add_gtest(config_parser_test
    test/unit/config_parser_test.cpp
  )
  if(TARGET config_parser_test)
    target_compile_definitions(config_parser_test PRIVATE
      ROS2_LIVEKIT_BRIDGE_CONFIG_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
    )
    target_link_libraries(config_parser_test
      ${PROJECT_NAME}
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
  list(APPEND AMENT_LINT_AUTO_EXCLUDE
    ament_cmake_uncrustify
    ament_cmake_clang_format
    ament_cmake_cpplint
  )
  ament_lint_auto_find_test_dependencies()
endif()

install(
  DIRECTORY "${CONFIG_PARSER_GENERATED_INCLUDE_DIR}/"
  DESTINATION "include/${PROJECT_NAME}"
)

ament_auto_package(USE_SCOPED_HEADER_INSTALL_DIR INSTALL_TO_SHARE schema)
