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

#ifndef ROS2_LIVEKIT_BRIDGE__UTILS__SCHEMA_TEXT_HPP_
#define ROS2_LIVEKIT_BRIDGE__UTILS__SCHEMA_TEXT_HPP_

#include <optional>
#include <string>

namespace ros2_livekit_bridge::utils {

/**
 * @brief A self-contained ROS2 message schema suitable for LiveKit data tracks.
 *
 * The @p encoding field is the rosbag2 definition encoding (for example
 * `"ros2msg"` or `"ros2idl"`). The @p text field is the full concatenated
 * definition text in MCAP format, including dependency sections separated by
 * `================================================================================`
 * delimiters.
 */
struct RosMessageSchema {
  //! @brief Definition encoding, e.g. `"ros2msg"` or `"ros2idl"`.
  std::string encoding;
  //! @brief Full concatenated message definition text.
  std::string text;
};

/**
 * @brief Render the full recursive ROS2 message definition for a topic type.
 *
 * Uses rosbag2_cpp to load `.msg` (or `.idl`) files from the local ament index
 * and concatenate dependencies into a single self-contained schema string.
 *
 * @param topic_type ROS message type in `pkg/msg/Type` form (as returned by
 * `get_topic_names_and_types`).
 * @return The rendered schema on success, or std::nullopt when the definition
 * cannot be resolved.
 */
std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type);

} // namespace ros2_livekit_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__SCHEMA_TEXT_HPP_
