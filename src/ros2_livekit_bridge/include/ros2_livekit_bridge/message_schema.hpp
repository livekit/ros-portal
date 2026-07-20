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

#ifndef ROS2_LIVEKIT_BRIDGE__MESSAGE_SCHEMA_HPP_
#define ROS2_LIVEKIT_BRIDGE__MESSAGE_SCHEMA_HPP_

#include <livekit/data_track_schema.h>

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
  std::string encoding;
  /// @brief Full concatenated message definition text.
  std::string text;
};

/// @brief Render the full recursive ROS2 message definition for a topic type.
///
/// Uses rosbag2_cpp to load `.msg` (or `.idl`) files from the local ament index
/// and concatenate dependencies into a single self-contained schema string.
///
/// @param topic_type ROS message type in `pkg/msg/Type` form (as returned by
/// `get_topic_names_and_types`).
/// @return The rendered schema on success, or std::nullopt when the definition
/// cannot be resolved.
std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type);

/// @brief Convert a ROS definition encoding to its LiveKit representation.
///
/// Supported custom encodings are preserved. Empty or oversized encodings fall
/// back to `Ros2Msg`.
///
/// @param encoding ROS definition encoding.
/// @return Matching LiveKit data track schema encoding.
livekit::DataTrackSchemaEncoding schemaEncodingFromRosDefinition(const std::string& encoding);

/// @brief Build the key used to deduplicate registered ROS schemas.
///
/// @param topic_type ROS message type in `pkg/msg/Type` form.
/// @param encoding ROS definition encoding.
/// @return A key unique to the encoding and topic type pair.
std::string schemaDedupeKey(const std::string& topic_type, const std::string& encoding);

/// @brief Calculate a SHA-256 fingerprint of exact schema text bytes.
///
/// No normalization is applied. Comments, whitespace, dependency ordering, and
/// every other byte in @p schema_text contribute to the result.
///
/// @param schema_text Schema definition text to fingerprint.
/// @return Lowercase 64-character hexadecimal SHA-256 digest.
std::string fingerprintSchemaText(const std::string& schema_text);

} // namespace ros2_livekit_bridge

#endif // ROS2_LIVEKIT_BRIDGE__MESSAGE_SCHEMA_HPP_
