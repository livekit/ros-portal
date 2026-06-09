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

#include "config/utils.hpp"

#include "ros2_livekit_bridge/config/error.hpp"

#include <sstream>
#include <string>

namespace ros2_livekit_bridge::config::utils
{

std::string fieldPath(const std::string & path, std::string_view field)
{
  return path + "." + std::string(field);
}

std::string nodeContext(const std::string & path, const YAML::Node & node)
{
  const auto mark = node.Mark();
  if (mark.line < 0 || mark.column < 0) {
    return path;
  }

  std::ostringstream context;
  context << path << " at line " << mark.line + 1 << ", column " <<
    mark.column + 1;
  return context.str();
}

void fail(
  const std::string & path,
  const YAML::Node & node,
  const std::string & expected,
  const std::string & detail)
{
  throw ConfigError(nodeContext(path, node), expected, detail);
}

void failMissing(
  const std::string & path,
  const std::string & expected)
{
  throw ConfigError(path, expected, "missing required field");
}

void requireMap(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsMap()) {
    fail(path, node, "map", "found non-map value");
  }
}

void requireSequence(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsSequence()) {
    fail(path, node, "sequence", "found non-sequence value");
  }
}

std::string scalarString(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsScalar()) {
    fail(path, node, "string", "found non-scalar value");
  }

  try {
    return node.as<std::string>();
  } catch (const YAML::Exception & e) {
    fail(path, node, "string", e.what());
  }
}

std::string requiredString(
  const YAML::Node & node,
  const std::string & key,
  const std::string & path)
{
  const auto value = node[key];
  const auto value_path = path + "." + key;
  if (!value) {
    failMissing(value_path, "string");
  }

  auto result = scalarString(value, value_path);
  if (result.empty()) {
    fail(value_path, value, "nonempty string", "found empty string");
  }
  return result;
}

int optionalPositiveInt(const YAML::Node & node, const std::string & path)
{
  if (!node || !node.IsScalar()) {
    fail(path, node, "positive integer", "found non-scalar value");
  }

  int result = 0;
  try {
    result = node.as<int>();
  } catch (const YAML::Exception & e) {
    fail(path, node, "positive integer", e.what());
  }

  if (result <= 0) {
    fail(path, node, "positive integer", "value must be greater than zero");
  }
  return result;
}

std::string mapKeyToString(const YAML::Node & key, const std::string & path)
{
  if (!key || !key.IsScalar()) {
    fail(path, key, "string key", "found non-scalar map key");
  }
  return key.as<std::string>();
}

void rejectUnknownFields(
  const YAML::Node & node,
  const std::set<std::string> & allowed,
  const std::string & path)
{
  requireMap(node, path);
  for (const auto & item : node) {
    const auto key = mapKeyToString(item.first, path);
    if (allowed.count(key) == 0) {
      fail(
        path + "." + key, item.first, "known field",
        "unknown field '" + key + "'");
    }
  }
}

} // namespace ros2_livekit_bridge::config::utils
