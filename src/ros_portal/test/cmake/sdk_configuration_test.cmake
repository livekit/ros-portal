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

cmake_minimum_required(VERSION 3.19)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(ConfigureLiveKitSDK)
include(LiveKitSDK)
unset(ENV{LIVEKIT_BUILD_SDK_FROM_SOURCE})
set(LIVEKIT_SDK_VERSION "1.13.0")

# Simulate each supported target with a cached capture bundle. No network or SDK
# checkout is needed, so the test also verifies that release builds are default.
function(check_target os processor triple digest extension)
  set(WIN32 FALSE)
  set(APPLE FALSE)
  set(UNIX FALSE)
  set(${os} TRUE)
  set(CMAKE_SYSTEM_PROCESSOR "${processor}")
  set(build_root "${TEST_ROOT}/${triple}")
  set(bundle "livekit-sdk-${triple}-capture-${LIVEKIT_SDK_VERSION}")
  file(MAKE_DIRECTORY "${build_root}/livekit-sdk/${bundle}/lib/cmake")
  foreach(variable LIVEKIT_BUILD_SDK_FROM_SOURCE LIVEKIT_SDK_SOURCE_DIR
      LIVEKIT_SDK_BUILD_JOBS LIVEKIT_SDK_SHA256)
    unset(${variable} CACHE)
  endforeach()
  livekit_configure_sdk(SOURCE_DIR "${TEST_ROOT}/missing-source" BUILD_ROOT "${build_root}")
  if(LIVEKIT_BUILD_SDK_FROM_SOURCE)
    message(FATAL_ERROR "Release SDK must be the default")
  endif()
  if(NOT LIVEKIT_SDK_INSTALL_ROOT STREQUAL "${build_root}/livekit-sdk/${bundle}")
    message(FATAL_ERROR "Incorrect capture bundle root: ${LIVEKIT_SDK_INSTALL_ROOT}")
  endif()
  if(NOT LIVEKIT_SDK_URL_USED STREQUAL
      "https://github.com/livekit/client-sdk-cpp/releases/download/v1.13.0/${bundle}.${extension}")
    message(FATAL_ERROR "Incorrect capture asset URL: ${LIVEKIT_SDK_URL_USED}")
  endif()
  # Check the built-in checksum separately because it is local to configure_sdk.
  # Intercept the setup helper and verify its SHA256 argument.
  function(livekit_sdk_setup)
    cmake_parse_arguments(SDK "" "SHA256" "" ${ARGN})
    if(NOT SDK_SHA256 STREQUAL digest)
      message(FATAL_ERROR "Incorrect checksum for ${triple}: ${SDK_SHA256}")
    endif()
  endfunction()
  livekit_configure_sdk(SOURCE_DIR "${TEST_ROOT}/missing-source" BUILD_ROOT "${build_root}")
endfunction()

# Run each case in a subprocess so function overrides and CMake caches are isolated.
if(TARGET_CASE)
  if(TARGET_CASE STREQUAL "linux-x64")
    check_target(UNIX x86_64 ubuntu-22.04-x64
      f9c06f863697dd2d59d33929a242dcd03a9d6f9a80ffd82857ed76e068600f9d tar.gz)
  elseif(TARGET_CASE STREQUAL "linux-arm64")
    check_target(UNIX aarch64 ubuntu-22.04-arm64
      2a17d086a5c66eee366ce033a37adf083d827ef6d428d5980efe3e6c4a17591f tar.gz)
  elseif(TARGET_CASE STREQUAL "macos-arm64")
    check_target(APPLE arm64 macos-arm64
      39b0786e9ea099492b9e38ba857df96006f1c8b1523b7d7c82ca6dff04149daa tar.gz)
  elseif(TARGET_CASE STREQUAL "windows-x64")
    check_target(WIN32 AMD64 windows-x64
      0a0f26995a250ab605e7e3916b7e467a379c2d4751cf5fb2e733b707c20b5a45 zip)
  elseif(TARGET_CASE STREQUAL "standard-cache")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(WIN32 FALSE)
    set(APPLE FALSE)
    set(UNIX TRUE)
    file(MAKE_DIRECTORY "${TEST_ROOT}/standard/livekit-sdk-ubuntu-22.04-x64-1.13.0/lib/cmake")
    livekit_sdk_setup(VERSION 1.13.0 SDK_DIR "${TEST_ROOT}/standard" NO_DOWNLOAD)
  elseif(TARGET_CASE STREQUAL "macos-x64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(WIN32 FALSE)
    set(APPLE TRUE)
    livekit_sdk_setup(VERSION 1.13.0 SDK_DIR "${TEST_ROOT}/intel" NO_DOWNLOAD)
  endif()
  return()
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
foreach(case linux-x64 linux-arm64 macos-arm64 windows-x64 standard-cache macos-x64)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DTEST_ROOT=${TEST_ROOT}"
    "-DTARGET_CASE=${case}" -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(case STREQUAL "standard-cache" OR case STREQUAL "macos-x64")
    if(result EQUAL 0 OR NOT "${error}" MATCHES "(NO_DOWNLOAD was set|Intel Macs)")
      message(FATAL_ERROR "${case} should reject the unavailable capture bundle: ${output}${error}")
    endif()
  elseif(NOT result EQUAL 0)
    message(FATAL_ERROR "${case} failed: ${output}${error}")
  endif()
endforeach()
file(REMOVE_RECURSE "${TEST_ROOT}")
message(STATUS "Capture SDK artifact selection checks passed")
