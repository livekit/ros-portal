/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

#include <sstream>

namespace ros2_livekit_bridge::ros2_cli
{

bool hasHiddenNameToken(const std::string & name)
{
  size_t token_start = 0;
  while (token_start < name.size()) {
    while (token_start < name.size() && name[token_start] == '/') {
      ++token_start;
    }
    if (token_start >= name.size()) {
      break;
    }

    const auto token_end = name.find('/', token_start);
    if (name[token_start] == '_') {
      return true;
    }
    if (token_end == std::string::npos) {
      break;
    }
    token_start = token_end + 1;
  }
  return false;
}

std::string joinTypes(const std::vector<std::string> & types)
{
  std::ostringstream stream;
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << types[i];
  }
  return stream.str();
}

} // namespace ros2_livekit_bridge::ros2_cli
