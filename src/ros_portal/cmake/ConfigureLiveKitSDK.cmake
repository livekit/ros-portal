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

include_guard(GLOBAL)

function(livekit_validate_sdk_configuration)
  if(NOT LIVEKIT_SDK_VERSION)
    message(FATAL_ERROR "LIVEKIT_SDK_VERSION must not be empty")
  endif()

  if(LIVEKIT_BUILD_SDK_FROM_SOURCE)
    if(NOT LIVEKIT_SDK_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR
        "LIVEKIT_SDK_BUILD_JOBS must be a positive integer, got: ${LIVEKIT_SDK_BUILD_JOBS}")
    endif()
    if(NOT EXISTS "${LIVEKIT_SDK_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "LIVEKIT_SDK_SOURCE_DIR is not an initialized SDK checkout: ${LIVEKIT_SDK_SOURCE_DIR}")
    endif()
  endif()

  if(LIVEKIT_SDK_SHA256)
    string(LENGTH "${LIVEKIT_SDK_SHA256}" _livekit_sdk_sha256_length)
    if(NOT _livekit_sdk_sha256_length EQUAL 64
        OR NOT LIVEKIT_SDK_SHA256 MATCHES "^[0-9A-Fa-f]+$")
      message(FATAL_ERROR
        "LIVEKIT_SDK_SHA256 must be a 64-character hexadecimal digest")
    endif()
  endif()
endfunction()

function(livekit_configure_sdk)
  set(one_value_args SOURCE_DIR BUILD_ROOT)
  cmake_parse_arguments(LIVEKIT_CONFIG "" "${one_value_args}" "" ${ARGN})

  if(NOT LIVEKIT_CONFIG_SOURCE_DIR)
    message(FATAL_ERROR "livekit_configure_sdk requires SOURCE_DIR")
  endif()
  if(NOT LIVEKIT_CONFIG_BUILD_ROOT)
    message(FATAL_ERROR "livekit_configure_sdk requires BUILD_ROOT")
  endif()

  set(_livekit_build_sdk_from_source_default ON)
  if(DEFINED ENV{BUILD_LIVEKIT_SDK_FROM_SOURCE})
    set(_livekit_build_sdk_from_source_default
      "$ENV{BUILD_LIVEKIT_SDK_FROM_SOURCE}")
  endif()
  option(LIVEKIT_BUILD_SDK_FROM_SOURCE
    "Build the pinned client-sdk-cpp checkout instead of downloading an SDK archive"
    "${_livekit_build_sdk_from_source_default}")

  set(LIVEKIT_SDK_SOURCE_DIR "${LIVEKIT_CONFIG_SOURCE_DIR}" CACHE PATH
    "Path to a client-sdk-cpp source checkout")
  set(_livekit_sdk_default_build_jobs 2)
  if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL}
      AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(_livekit_sdk_default_build_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
  endif()
  set(LIVEKIT_SDK_BUILD_JOBS "${_livekit_sdk_default_build_jobs}" CACHE STRING
    "Maximum parallel jobs for a LiveKit SDK source build")
  set(LIVEKIT_SDK_SHA256 "" CACHE STRING
    "Optional SHA-256 checksum for the LiveKit C++ SDK archive")

  if(NOT LIVEKIT_SDK_SHA256 AND LIVEKIT_SDK_VERSION STREQUAL "1.7.0-rc2")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _livekit_target_processor)
    if(APPLE AND _livekit_target_processor MATCHES "^(arm64|aarch64)$")
      set(LIVEKIT_SDK_SHA256 "610bbf9bb45bf7d99272a5b2acf9585defb9d46c454ef966e31ef573261da3f7")
    elseif(APPLE AND _livekit_target_processor MATCHES "^(x86_64|amd64)$")
      set(LIVEKIT_SDK_SHA256 "ef1d242174063ce52bbe1de8b464cf301fb54bcb79cc52b1c6cdde481c888998")
    elseif(UNIX AND _livekit_target_processor MATCHES "^(arm64|aarch64)$")
      set(LIVEKIT_SDK_SHA256 "dcacee92e9fba3d6546af1bd7b284b8672e879a6f74ab13ebf7945a2595f5a8b")
    elseif(UNIX AND _livekit_target_processor MATCHES "^(x86_64|amd64)$")
      set(LIVEKIT_SDK_SHA256 "55c79218b2dff12fd8ea3e78ffd28d07e0fab6d26ac151a907a33e08407bf039")
    elseif(WIN32 AND _livekit_target_processor MATCHES "^(x86_64|amd64)$")
      set(LIVEKIT_SDK_SHA256 "62928a72a8fe0e3b2899cd0233d301bee5bf4e5146cd17bf2fe97e81a31cd5d4")
    endif()
  endif()

  livekit_validate_sdk_configuration()

  if(LIVEKIT_BUILD_SDK_FROM_SOURCE)
    include(BuildLiveKitSDK)
    set(_livekit_source_install_dir
      "${LIVEKIT_CONFIG_BUILD_ROOT}/livekit-sdk-source-install")
    livekit_build_sdk_from_source(
      SOURCE_DIR "${LIVEKIT_SDK_SOURCE_DIR}"
      BUILD_DIR "${LIVEKIT_CONFIG_BUILD_ROOT}/livekit-sdk-source-build"
      INSTALL_DIR "${_livekit_source_install_dir}"
      JOBS "${LIVEKIT_SDK_BUILD_JOBS}"
      VERSION "${LIVEKIT_SDK_VERSION}"
    )
    set(LIVEKIT_SDK_INSTALL_ROOT "${_livekit_source_install_dir}")
    list(PREPEND CMAKE_PREFIX_PATH "${LIVEKIT_SDK_INSTALL_ROOT}")
  else()
    include(LiveKitSDK)
    set(_livekit_sdk_setup_args
      VERSION "${LIVEKIT_SDK_VERSION}"
      SDK_DIR "${LIVEKIT_CONFIG_BUILD_ROOT}/livekit-sdk"
      GITHUB_TOKEN "$ENV{GITHUB_TOKEN}"
    )
    if(LIVEKIT_SDK_SHA256)
      list(APPEND _livekit_sdk_setup_args SHA256 "${LIVEKIT_SDK_SHA256}")
    endif()
    livekit_sdk_setup(${_livekit_sdk_setup_args})
    set(LIVEKIT_SDK_INSTALL_ROOT "${LIVEKIT_SDK_EXTRACTED_ROOT}")
  endif()

  set(LIVEKIT_SDK_INSTALL_ROOT "${LIVEKIT_SDK_INSTALL_ROOT}" PARENT_SCOPE)
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  if(DEFINED LiveKit_DIR)
    set(LiveKit_DIR "${LiveKit_DIR}" PARENT_SCOPE)
  endif()
endfunction()
