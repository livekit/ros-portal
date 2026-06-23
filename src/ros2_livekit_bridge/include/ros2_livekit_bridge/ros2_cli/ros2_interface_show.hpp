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

#pragma once

#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"
#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

namespace ros2_livekit_bridge::ros2_cli::interface_show
{

/**
 * @brief Parsing and rendering helpers for `ros2 interface show`.
 */
namespace utils
{

/**
 * @brief Split an interface type identifier on `/` separators.
 * @param type Interface type identifier, such as `std_msgs/msg/String`.
 * @return Slash-delimited tokens, including a trailing empty token when @p type
 * ends with `/`.
 */
inline std::vector<std::string> splitInterfaceType(const std::string & type)
{
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

/**
 * @brief Resolve `package/msg/Name` (or srv/action) to its installed definition path.
 * @param type Fully qualified interface type identifier.
 * @return Installed definition file path, or std::nullopt when @p type is malformed
 * or not present in the ament index.
 */
inline std::optional<std::string> interfacePath(const std::string & type)
{
  const auto parts = splitInterfaceType(type);
  if (parts.size() != 3 || parts[0].empty() || parts[1].empty() ||
    parts[2].empty())
  {
    return std::nullopt;
  }

  const auto & package_name = parts[0];
  const auto & interface_kind = parts[1];
  const auto & interface_name = parts[2];
  if (interface_kind != "msg" && interface_kind != "srv" &&
    interface_kind != "action")
  {
    return std::nullopt;
  }

  try {
    const auto share_directory =
      ament_index_cpp::get_package_share_directory(package_name);
    const auto extension = "." + interface_kind;
    return share_directory + "/" + interface_kind + "/" + interface_name +
           extension;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

/**
 * @brief Remove array and bounded-string suffixes from a field type token.
 * @param type Field type token from an interface definition line.
 * @return Base type token without `[...]` or `<...>` suffixes.
 */
inline std::string removeArraySuffix(std::string type)
{
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

/**
 * @brief Return true when the field type token is a ROS primitive scalar/string.
 * @param type Base field type token.
 */
inline bool isPrimitiveInterfaceType(const std::string & type)
{
  static const std::set<std::string> kPrimitives{
    "bool", "byte", "char", "float32", "float64",
    "int8", "uint8", "int16", "uint16", "int32",
    "uint32", "int64", "uint64", "string", "wstring",
  };
  return kPrimitives.count(type) > 0;
}

/**
 * @brief Remove a trailing ROS interface comment from one definition line.
 * @param line One line from an interface definition file.
 * @return Line content with trailing comments and whitespace removed.
 */
inline std::string stripTrailingComment(const std::string & line)
{
  const auto comment_start = line.find('#');
  if (comment_start == std::string::npos) {
    return ros2_cli::rightTrim(line);
  }
  return ros2_cli::rightTrim(line.substr(0, comment_start));
}

/**
 * @brief Infer a nested message interface referenced by one definition line.
 * @param package_name Package name of the enclosing interface definition.
 * @param line One line from an interface definition file.
 * @return Nested message type identifier, or std::nullopt when the line does not
 * reference a nested message.
 */
inline std::optional<std::string>
nestedInterfaceTypeFromLine(const std::string & package_name, const std::string & line)
{
  const auto without_comment = stripTrailingComment(line);
  const auto trimmed = ros2_cli::leftTrim(without_comment);
  if (trimmed.empty() || trimmed == "---") {
    return std::nullopt;
  }

  std::istringstream stream(trimmed);
  std::string type_token;
  std::string name_token;
  stream >> type_token >> name_token;
  if (type_token.empty() || name_token.empty() ||
    name_token.find('=') != std::string::npos)
  {
    return std::nullopt;
  }

  type_token = removeArraySuffix(type_token);
  if (isPrimitiveInterfaceType(type_token)) {
    return std::nullopt;
  }

  const auto parts = splitInterfaceType(type_token);
  if (parts.size() == 1) {
    if (!type_token.empty() &&
      std::isupper(static_cast<unsigned char>(type_token.front())))
    {
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

/**
 * @brief Append one rendered definition line using the requested comment mode.
 * @param output Rendered definition accumulator.
 * @param line One line from an interface definition file.
 * @param show_comments When true, preserve comments and blank lines.
 * @param indent_level Number of tab characters to prefix.
 */
inline void appendRenderedInterfaceLine(
  std::ostringstream & output, const std::string & line, bool show_comments,
  int indent_level)
{
  if (show_comments) {
    if (!line.empty()) {
      output << std::string(static_cast<size_t>(indent_level), '\t') << line;
    }
    output << '\n';
    return;
  }

  const auto trimmed = ros2_cli::leftTrim(line);
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

}  // namespace utils

}  // namespace ros2_livekit_bridge::ros2_cli::interface_show

namespace ros2_livekit_bridge::ros2_cli
{

/**
 * @brief Render the definition for a ROS interface type.
 * @param options Interface type and comment filtering options.
 * @return Text equivalent to `ros2 interface show`, including nested message
 * definitions when referenced by the root interface, or std::nullopt when the
 * type is empty, unsupported, malformed, or cannot be found in the ament index.
 */
std::optional<std::string>
renderInterfaceDefinition(const InterfaceShowOptions & options);

}  // namespace ros2_livekit_bridge::ros2_cli
