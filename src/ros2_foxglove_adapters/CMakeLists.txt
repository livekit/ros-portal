cmake_minimum_required(VERSION 3.20)
project(ros2_foxglove_adapters)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This package does not use the LiveKit SDK directly, but colcon forwards
# package-specific CMake args to all packages in a build graph. Touch these
# variables so `--cmake-args -DLIVEKIT_SDK_DIR=...` doesn't emit "unused"
# warnings here.
if(DEFINED LIVEKIT_SDK_DIR)
  set(_ignored_livekit_sdk_dir "${LIVEKIT_SDK_DIR}")
endif()
if(DEFINED LIVEKIT_LOCAL_SDK_DIR)
  set(_ignored_livekit_local_sdk_dir "${LIVEKIT_LOCAL_SDK_DIR}")
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(Protobuf REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

set(FOXGLOVE_SCHEMA_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../externals/foxglove-sdk/schemas/proto")

if(NOT EXISTS "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Pose.proto")
  message(FATAL_ERROR
    "Expected Foxglove schemas at ${FOXGLOVE_SCHEMA_ROOT}. "
    "Import externals with 'vcs import --skip-existing src/externals < external.repos'.")
endif()

set(FOXGLOVE_PROTO_FILES
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Vector2.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Vector3.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Quaternion.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/PackedElementField.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Pose.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/PoseInFrame.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/PosesInFrame.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Grid.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/FrameTransform.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/PointCloud.proto"
  "${FOXGLOVE_SCHEMA_ROOT}/foxglove/Log.proto")

if(TARGET protobuf::protoc)
  set(FOXGLOVE_PROTOC_COMMAND protobuf::protoc)
elseif(Protobuf_PROTOC_EXECUTABLE)
  set(FOXGLOVE_PROTOC_COMMAND "${Protobuf_PROTOC_EXECUTABLE}")
else()
  message(FATAL_ERROR "Could not find the protoc executable")
endif()

set(FOXGLOVE_PROTO_SRCS)
set(FOXGLOVE_PROTO_HDRS)
foreach(FOXGLOVE_PROTO ${FOXGLOVE_PROTO_FILES})
  get_filename_component(FOXGLOVE_PROTO_NAME "${FOXGLOVE_PROTO}" NAME_WLE)
  set(FOXGLOVE_GENERATED_SRC
    "${CMAKE_CURRENT_BINARY_DIR}/foxglove/${FOXGLOVE_PROTO_NAME}.pb.cc")
  set(FOXGLOVE_GENERATED_HDR
    "${CMAKE_CURRENT_BINARY_DIR}/foxglove/${FOXGLOVE_PROTO_NAME}.pb.h")

  add_custom_command(
    OUTPUT "${FOXGLOVE_GENERATED_SRC}" "${FOXGLOVE_GENERATED_HDR}"
    COMMAND ${FOXGLOVE_PROTOC_COMMAND}
    ARGS
      -I "${FOXGLOVE_SCHEMA_ROOT}"
      --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
      "${FOXGLOVE_PROTO}"
    DEPENDS "${FOXGLOVE_PROTO}"
    COMMENT "Generating Foxglove protobuf for ${FOXGLOVE_PROTO_NAME}.proto"
    VERBATIM)

  list(APPEND FOXGLOVE_PROTO_SRCS "${FOXGLOVE_GENERATED_SRC}")
  list(APPEND FOXGLOVE_PROTO_HDRS "${FOXGLOVE_GENERATED_HDR}")
endforeach()

set_source_files_properties(${FOXGLOVE_PROTO_SRCS} ${FOXGLOVE_PROTO_HDRS}
  PROPERTIES GENERATED TRUE)

add_custom_target(${PROJECT_NAME}_generated_protos
  DEPENDS ${FOXGLOVE_PROTO_SRCS} ${FOXGLOVE_PROTO_HDRS})

add_library(${PROJECT_NAME} SHARED
  src/to_foxglove.cpp
  ${FOXGLOVE_PROTO_SRCS})
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_generated_protos)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(${PROJECT_NAME} protobuf::libprotobuf)

ament_target_dependencies(${PROJECT_NAME}
  builtin_interfaces
  geometry_msgs
  nav_msgs
  sensor_msgs
  std_msgs)

install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(DIRECTORY include/
  DESTINATION include)

install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/foxglove"
  DESTINATION include)

install(FILES ${FOXGLOVE_PROTO_FILES}
  DESTINATION share/${PROJECT_NAME}/proto/foxglove)

ament_export_dependencies(
  builtin_interfaces
  geometry_msgs
  nav_msgs
  Protobuf
  sensor_msgs
  std_msgs)
ament_export_include_directories(include)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
