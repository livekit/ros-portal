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

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

std::string requiredStringField(
  const nlohmann::json & body,
  const char *field_name,
  const char *missing_message,
  const char *empty_message)
{
  const auto field = body.find(field_name);
  if (field == body.end() || !field->is_string()) {
    throw std::invalid_argument(missing_message);
  }

  auto value = rightTrim(leftTrim(field->get<std::string>()));
  if (value.empty()) {
    throw std::invalid_argument(empty_message);
  }
  return value;
}

bool topicTypeMatches(
  const std::vector<std::string> & graph_types,
  const std::string & msg_type)
{
  return std::find(graph_types.begin(), graph_types.end(), msg_type) !=
         graph_types.end();
}

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
