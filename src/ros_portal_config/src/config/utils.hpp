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

#ifndef ROS_PORTAL_CONFIG_CONFIG_UTILS_HPP_
#define ROS_PORTAL_CONFIG_CONFIG_UTILS_HPP_

#include <yaml-cpp/yaml.h>

#include <set>
#include <string>
#include <string_view>

// Schema-agnostic YAML validation helpers shared by the config parser. These
// translate yaml-cpp nodes into typed values and raise ConfigError with a
// human-readable location when the document does not match expectations. They
// are intentionally free of any RosPortalConfig schema knowledge so they can be
// unit tested and reused independently of the config layout.
namespace ros_portal_config::utils {

// Joins a parent path and a field name into a dotted path (e.g. "$.topics").
std::string fieldPath(const std::string& path, std::string_view field);

// Annotates a path with the node's source line and column when available.
std::string nodeContext(const std::string& path, const YAML::Node& node);

// Throws ConfigError describing a node that failed validation.
[[noreturn]] void fail(const std::string& path, const YAML::Node& node, const std::string& expected,
                       const std::string& detail);

// Throws ConfigError describing a required field that is absent.
[[noreturn]] void failMissing(const std::string& path, const std::string& expected);

// Validates that the node is a map / sequence, failing otherwise.
void requireMap(const YAML::Node& node, const std::string& path);
void requireSequence(const YAML::Node& node, const std::string& path);

// Returns the node as a string, failing if it is not a scalar.
std::string scalarString(const YAML::Node& node, const std::string& path);

// Returns a required, nonempty string field from a map node.
std::string requiredString(const YAML::Node& node, const std::string& key, const std::string& path);

// Returns a scalar parsed as an integer that must be greater than zero.
int optionalPositiveInt(const YAML::Node& node, const std::string& path);

// Returns a scalar map key as a string, failing on non-scalar keys.
std::string mapKeyToString(const YAML::Node& key, const std::string& path);

// Fails if the map node contains any key outside the allowed set.
void rejectUnknownFields(const YAML::Node& node, const std::set<std::string>& allowed, const std::string& path);

} // namespace ros_portal_config::utils

#endif // ROS_PORTAL_CONFIG_CONFIG_UTILS_HPP_
