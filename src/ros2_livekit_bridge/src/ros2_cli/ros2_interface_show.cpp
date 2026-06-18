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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>

namespace ros2_livekit_bridge::ros2_cli {
namespace {

// Remove leading whitespace from a string.
std::string leftTrim(const std::string &value) {
  const auto first =
      std::find_if(value.begin(), value.end(), [](unsigned char character) {
        return !std::isspace(character);
      });
  return std::string(first, value.end());
}

// Remove trailing whitespace from a string.
std::string rightTrim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

// Split an interface type identifier on `/` separators.
std::vector<std::string> splitInterfaceType(const std::string &type) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= type.size()) {
    const auto end = type.find('/', start);
    parts.push_back(type.substr(start, end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

// Resolve package/msg/Name (or srv/action) to its installed definition file path.
std::string interfacePath(const std::string &type) {
  const auto parts = splitInterfaceType(type);
  if (parts.size() != 3 || parts[0].empty() || parts[1].empty() ||
      parts[2].empty()) {
    throw std::runtime_error("Invalid name '" + type +
                             "'. Expected three parts separated by '/'");
  }

  const auto &package_name = parts[0];
  const auto &interface_kind = parts[1];
  const auto &interface_name = parts[2];
  if (interface_kind != "msg" && interface_kind != "srv" &&
      interface_kind != "action") {
    throw std::runtime_error("Invalid interface kind '" + interface_kind +
                             "'. Expected 'msg', 'srv', or 'action'");
  }

  const auto share_directory =
      ament_index_cpp::get_package_share_directory(package_name);
  const auto extension = "." + interface_kind;
  return share_directory + "/" + interface_kind + "/" + interface_name +
         extension;
}

// Remove array and bounded-string suffixes from a field type token.
std::string removeArraySuffix(std::string type) {
  const auto array_start = type.find('[');
  if (array_start != std::string::npos) {
    type = type.substr(0, array_start);
  }

  const auto bound_start = type.find('<');
  if (bound_start != std::string::npos) {
    type = type.substr(0, bound_start);
  }
  return type;
}

// Return true when the field type token is a ROS primitive scalar/string.
bool isPrimitiveInterfaceType(const std::string &type) {
  static const std::set<std::string> kPrimitives{
      "bool",   "byte",  "char",   "float32", "float64",
      "int8",   "uint8", "int16",  "uint16",  "int32",
      "uint32", "int64", "uint64", "string",  "wstring",
  };
  return kPrimitives.count(type) > 0;
}

// Remove a trailing ROS interface comment from one definition line.
std::string stripTrailingComment(const std::string &line) {
  const auto comment_start = line.find('#');
  if (comment_start == std::string::npos) {
    return rightTrim(line);
  }
  return rightTrim(line.substr(0, comment_start));
}

// Infer a nested message interface referenced by one definition line.
std::optional<std::string>
nestedInterfaceTypeFromLine(const std::string &package_name,
                            const std::string &line) {
  const auto without_comment = stripTrailingComment(line);
  const auto trimmed = leftTrim(without_comment);
  if (trimmed.empty() || trimmed == "---") {
    return std::nullopt;
  }

  std::istringstream stream(trimmed);
  std::string type_token;
  std::string name_token;
  stream >> type_token >> name_token;
  if (type_token.empty() || name_token.empty() ||
      name_token.find('=') != std::string::npos) {
    return std::nullopt;
  }

  type_token = removeArraySuffix(type_token);
  if (isPrimitiveInterfaceType(type_token)) {
    return std::nullopt;
  }

  const auto parts = splitInterfaceType(type_token);
  if (parts.size() == 1) {
    if (!type_token.empty() &&
        std::isupper(static_cast<unsigned char>(type_token.front()))) {
      return package_name + "/msg/" + type_token;
    }
    return std::nullopt;
  }
  if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
    return parts[0] + "/msg/" + parts[1];
  }
  if (parts.size() == 3 && parts[1] == "msg") {
    return type_token;
  }
  return std::nullopt;
}

// Append one rendered definition line using the requested comment mode.
void appendRenderedInterfaceLine(std::ostringstream &output,
                                 const std::string &line, bool show_comments,
                                 int indent_level) {
  if (show_comments) {
    if (!line.empty()) {
      output << std::string(static_cast<size_t>(indent_level), '\t') << line;
    }
    output << '\n';
    return;
  }

  const auto trimmed = leftTrim(line);
  if (trimmed.empty() || trimmed.front() == '#') {
    return;
  }

  const auto without_comment = stripTrailingComment(line);
  if (without_comment.empty()) {
    return;
  }
  output << std::string(static_cast<size_t>(indent_level), '\t')
         << without_comment << '\n';
}

// Render an interface definition and recursively inline nested messages.
void renderInterfaceDefinitionRecursive(const std::string &type,
                                        bool show_comments,
                                        bool show_nested_comments,
                                        int indent_level,
                                        std::set<std::string> &active_types,
                                        std::ostringstream &output) {
  const auto parts = splitInterfaceType(type);
  const auto path = interfacePath(type);
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("Could not find interface '" + type + "'");
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
      renderInterfaceDefinitionRecursive(*nested_type, show_nested_comments,
                                         show_nested_comments, indent_level + 1,
                                         active_types, output);
    }
  }

  active_types.erase(type);
}

} // namespace

std::string renderInterfaceDefinition(const InterfaceShowOptions &options) {
  if (options.type.empty()) {
    throw std::runtime_error("the passed value is empty");
  }
  if (options.type == "-") {
    throw std::runtime_error("expected stdin pipe");
  }
  if (options.all_comments && options.no_comments) {
    throw std::runtime_error(
        "all_comments and no_comments are mutually exclusive");
  }

  std::set<std::string> active_types;
  std::ostringstream output;
  renderInterfaceDefinitionRecursive(options.type, !options.no_comments,
                                     options.all_comments, 0, active_types,
                                     output);
  return output.str();
}

} // namespace ros2_livekit_bridge::ros2_cli
