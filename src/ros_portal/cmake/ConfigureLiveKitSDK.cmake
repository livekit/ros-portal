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

  set(_livekit_build_sdk_from_source_default OFF)
  if(DEFINED ENV{LIVEKIT_BUILD_SDK_FROM_SOURCE})
    set(_livekit_build_sdk_from_source_default
      "$ENV{LIVEKIT_BUILD_SDK_FROM_SOURCE}")
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

  include(LiveKitSDK)
  _lk_default_triple(_livekit_sdk_triple)

  if(NOT LIVEKIT_SDK_SHA256 AND LIVEKIT_SDK_VERSION STREQUAL "1.11.0")
    if(_livekit_sdk_triple STREQUAL "ubuntu-22.04-x64")
      set(LIVEKIT_SDK_SHA256
        "4946379fea10b84b9f6cac8ec1560255ca81a1b3d2034db8178e7a8a699cf56a")
    elseif(_livekit_sdk_triple STREQUAL "ubuntu-22.04-arm64")
      set(LIVEKIT_SDK_SHA256
        "5f17bb13eadb9512c1f4b6a68f31ca0b836d21a5498ca1ca03f0aae0062127f5")
    elseif(_livekit_sdk_triple STREQUAL "macos-arm64")
      set(LIVEKIT_SDK_SHA256
        "b6650d8719c5acf26c39de0db2589bd687a5437b61ab014708e7f9ce4af5b712")
    elseif(_livekit_sdk_triple STREQUAL "macos-x64")
      set(LIVEKIT_SDK_SHA256
        "bd28fff5941586c73e1e33ffdbdcc755624def0ce3bea5684719267f0b675ac5")
    elseif(_livekit_sdk_triple STREQUAL "windows-x64")
      set(LIVEKIT_SDK_SHA256
        "d95d677c3ca7348e0af18ede713f7fe825c14a726296ff65524b933f64f965dc")
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
