cmake_minimum_required(VERSION 3.8)
project(imx219_camera)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)

# In-process IPA interposer. LD_PRELOAD'ed into the camera process by the launch
# file so libcamera loads its (genuine) Raspberry Pi IPA in-process instead of
# in a crashing isolated worker. See src/libcamera_ipa_inprocess.cpp for the
# full rationale. It intentionally has no dependencies: it only redefines one
# libcamera symbol.
add_library(camera_ipa_inprocess SHARED src/libcamera_ipa_inprocess.cpp)
# The interposing symbol must be exported even under a hidden default preset.
set_target_properties(camera_ipa_inprocess PROPERTIES
  CXX_VISIBILITY_PRESET default
  VISIBILITY_INLINES_HIDDEN OFF)

install(TARGETS camera_ipa_inprocess
  LIBRARY DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

install(PROGRAMS scripts/setup_camera_udev.sh
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
