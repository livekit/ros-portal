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

#include "ros2_livekit_bridge/ros2_cli/ros2_interface_show.hpp"

#include <fstream>
#include <set>
#include <sstream>

namespace ros2_livekit_bridge::ros2_cli
{
namespace
{

using interface_show::utils::appendRenderedInterfaceLine;
using interface_show::utils::interfacePath;
using interface_show::utils::nestedInterfaceTypeFromLine;
using interface_show::utils::splitInterfaceType;

// Render an interface definition and recursively inline nested messages.
bool renderInterfaceDefinitionRecursive(
  const std::string & type, bool show_comments, bool show_nested_comments,
  int indent_level, std::set<std::string> & active_types,
  std::ostringstream & output)
{
  const auto parts = splitInterfaceType(type);
  const auto path = interfacePath(type);
  if (!path.has_value()) {
    return false;
  }

  std::ifstream input(*path);
  if (!input.is_open()) {
    return false;
  }

  active_types.insert(type);

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    appendRenderedInterfaceLine(output, line, show_comments, indent_level);

    const auto nested_type = nestedInterfaceTypeFromLine(parts[0], line);
    if (nested_type && active_types.count(*nested_type) == 0) {
      if (!renderInterfaceDefinitionRecursive(
          *nested_type, show_nested_comments, show_nested_comments,
          indent_level + 1, active_types, output))
      {
        active_types.erase(type);
        return false;
      }
    }
  }

  active_types.erase(type);
  return true;
}

}  // namespace

std::optional<std::string>
renderInterfaceDefinition(const InterfaceShowOptions & options)
{
  if (options.type.empty()) {
    return std::nullopt;
  }
  if (options.type == "-") {
    return std::nullopt;
  }
  if (options.all_comments && options.no_comments) {
    return std::nullopt;
  }

  std::set<std::string> active_types;
  std::ostringstream output;
  if (!renderInterfaceDefinitionRecursive(
      options.type, !options.no_comments, options.all_comments, 0, active_types,
      output))
  {
    return std::nullopt;
  }
  return output.str();
}

}  // namespace ros2_livekit_bridge::ros2_cli
