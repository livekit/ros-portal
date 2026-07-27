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

#include <optional>
#include <string>

namespace ros2_livekit_bridge {

/// @brief A self-contained ROS2 message schema suitable for LiveKit data tracks.
///
/// The @p encoding field is the rosbag2 definition encoding (for example
/// `"ros2msg"` or `"ros2idl"`). The @p text field is the full concatenated
/// definition text in MCAP format, including dependency sections separated by
/// `================================================================================`
/// delimiters.
struct RosMessageSchema {
  /// @brief Definition encoding, e.g. `"ros2msg"` or `"ros2idl"`.
  /// @note String because rosbag2 returns encoding as one.
  std::string encoding;
  /// @brief Full concatenated message definition text.
  std::string text;
};

/// @brief Renders ROS message definitions from the local ament index.
///
/// Two interchangeable backends exist because the rosbag2 API this is built on
/// (`rosbag2_cpp::LocalMessageDefinitionSource` and
/// `rosbag2_storage::MessageDefinition`) does not exist on every supported
/// distribution — Humble predates it. @ref renderDefinition picks the right one
/// for the build, and is the only entry point production code should use.
///
/// Both backends are declared unconditionally so this header is identical on
/// every distribution and both are exercised by the same tests. The backends
/// must agree byte-for-byte: schema identity is a SHA-256 over @ref
/// RosMessageSchema::text, so any divergence changes schema hashes and makes
/// bridges reject each other's data tracks.
namespace schema {

/// @brief Whether this build has the rosbag2 message-definition API.
///
/// Determined at configure time by probing for the rosbag2 header, not by
/// distribution name, so a distribution that gains or loses the API is handled
/// without a code change.
bool hasRosbag2DefinitionSource();

/// @brief Render a definition using the bundled definition source.
///
/// Compiled on every distribution. Reimplements rosbag2's traversal — same
/// concatenation format, same sorted depth-first dependency ordering — over the
/// `rosidl_interfaces` ament resource, falling back from `.msg` to `.idl` when a
/// type has no message definition.
///
/// @param topic_type ROS message type, e.g. `"std_msgs/msg/String"`.
/// @return The rendered schema, or @c std::nullopt if it cannot be rendered.
std::optional<RosMessageSchema> renderBundledDefinition(const std::string& topic_type);

/// @brief Render a definition using rosbag2's `LocalMessageDefinitionSource`.
///
/// @param topic_type ROS message type, e.g. `"std_msgs/msg/String"`.
/// @return The rendered schema; @c std::nullopt if it cannot be rendered, or
///   always @c std::nullopt when @ref hasRosbag2DefinitionSource is false.
std::optional<RosMessageSchema> renderRosbag2Definition(const std::string& topic_type);

/// @brief Render a definition using the preferred source for this build.
///
/// Uses rosbag2 where available and the bundled source otherwise, so callers do
/// not need to know which distribution they are on.
///
/// @param topic_type ROS message type, e.g. `"std_msgs/msg/String"`.
/// @return The rendered schema, or @c std::nullopt if it cannot be rendered.
std::optional<RosMessageSchema> renderDefinition(const std::string& topic_type);

} // namespace schema
} // namespace ros2_livekit_bridge
