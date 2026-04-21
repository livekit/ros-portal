cmake_minimum_required(VERSION 3.20)
project(generic_sub_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(generic_sub_node src/generic_sub_node.cpp)

target_link_libraries(generic_sub_node yaml-cpp)
ament_target_dependencies(generic_sub_node rclcpp)

install(TARGETS generic_sub_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(sensor_msgs REQUIRED)
  find_package(rosbag2_cpp REQUIRED)
  find_package(rosbag2_storage REQUIRED)

  ament_add_gtest(test_cdr_mcap_roundtrip test/test_cdr_mcap_roundtrip.cpp)
  ament_target_dependencies(test_cdr_mcap_roundtrip
    rclcpp sensor_msgs rosbag2_cpp rosbag2_storage)
endif()

ament_package()
