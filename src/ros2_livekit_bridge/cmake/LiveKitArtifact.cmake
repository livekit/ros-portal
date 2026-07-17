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

# Import a raw LiveKit Linux artifact produced by client-sdk-cpp CI.
function(livekit_import_artifact sdk_root)
  if(NOT UNIX OR APPLE)
    message(FATAL_ERROR
      "The temporary LiveKit CI artifact is Linux-only.")
  endif()

  set(_livekit_include_dir "${sdk_root}/include")
  set(_livekit_library "${sdk_root}/lib/liblivekit.so")
  set(_livekit_ffi_library "${sdk_root}/lib/liblivekit_ffi.so")

  foreach(_required_path IN ITEMS
      "${_livekit_include_dir}/livekit/data_track_options.h"
      "${_livekit_include_dir}/livekit/data_track_schema.h"
      "${_livekit_library}"
      "${_livekit_ffi_library}")
    if(NOT EXISTS "${_required_path}")
      message(FATAL_ERROR
        "LiveKit SDK artifact is incomplete; missing:\n"
        "  ${_required_path}\n"
        "Run tools/fetch-livekit-sdk-artifact.sh on the host.")
    endif()
  endforeach()

  add_library(LiveKit::livekit SHARED IMPORTED GLOBAL)
  set_target_properties(LiveKit::livekit PROPERTIES
    IMPORTED_LOCATION "${_livekit_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${_livekit_include_dir}"
  )

  set(LIVEKIT_ARTIFACT_LIBRARY_DIR "${sdk_root}/lib" PARENT_SCOPE)
endfunction()
