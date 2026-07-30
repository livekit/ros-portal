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

function(_livekit_run_source_build_step description)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "${description} failed with exit code ${_result}\n"
      "stdout:\n${_stdout}\n"
      "stderr:\n${_stderr}")
  endif()
endfunction()

# Build and install the checked-out LiveKit SDK in an isolated CMake tree.
#
# Arguments:
#   SOURCE_DIR  client-sdk-cpp checkout
#   BUILD_DIR   isolated SDK build directory
#   INSTALL_DIR SDK install prefix returned to find_package(LiveKit)
#   JOBS        maximum parallel SDK build jobs
#   VERSION     SDK version passed to the source build
function(livekit_build_sdk_from_source)
  set(one_value_args SOURCE_DIR BUILD_DIR INSTALL_DIR JOBS VERSION)
  cmake_parse_arguments(LIVEKIT_SOURCE "" "${one_value_args}" "" ${ARGN})

  foreach(required_arg SOURCE_DIR BUILD_DIR INSTALL_DIR JOBS VERSION)
    if(NOT LIVEKIT_SOURCE_${required_arg})
      message(FATAL_ERROR
        "livekit_build_sdk_from_source requires ${required_arg}")
    endif()
  endforeach()

  if(NOT EXISTS "${LIVEKIT_SOURCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "LiveKit SDK source is not initialized at ${LIVEKIT_SOURCE_SOURCE_DIR}.\n"
      "Run: vcs import --recursive src/externals < external.repos")
  endif()
  if(NOT EXISTS
      "${LIVEKIT_SOURCE_SOURCE_DIR}/client-sdk-rust/livekit-ffi/Cargo.toml")
    message(FATAL_ERROR
      "LiveKit SDK nested repositories are not initialized.\n"
      "Run: vcs import --recursive src/externals < external.repos")
  endif()

  message(STATUS
    "Building LiveKit SDK ${LIVEKIT_SOURCE_VERSION} from "
    "${LIVEKIT_SOURCE_SOURCE_DIR}")
  _livekit_run_source_build_step(
    "LiveKit SDK configure"
    "${CMAKE_COMMAND}"
    -S "${LIVEKIT_SOURCE_SOURCE_DIR}"
    -B "${LIVEKIT_SOURCE_BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${LIVEKIT_SOURCE_INSTALL_DIR}
    -DLIVEKIT_BUILD_EXAMPLES=OFF
    -DLIVEKIT_BUILD_TESTS=OFF
    -DLIVEKIT_VERSION=${LIVEKIT_SOURCE_VERSION}
  )
  _livekit_run_source_build_step(
    "LiveKit SDK build"
    "${CMAKE_COMMAND}" -E env
    "CARGO_BUILD_JOBS=${LIVEKIT_SOURCE_JOBS}"
    "${CMAKE_COMMAND}" --build "${LIVEKIT_SOURCE_BUILD_DIR}"
    --parallel "${LIVEKIT_SOURCE_JOBS}"
  )
  _livekit_run_source_build_step(
    "LiveKit SDK install"
    "${CMAKE_COMMAND}" --install "${LIVEKIT_SOURCE_BUILD_DIR}"
  )
endfunction()
