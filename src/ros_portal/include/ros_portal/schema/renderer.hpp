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

namespace ros_portal {

/// @brief A self-contained ROS2 message schema suitable for LiveKit data tracks.
struct RosMessageSchema {
  /// @brief Definition encoding, e.g. `"ros2msg"` or `"ros2idl"`.
  std::string encoding;
  /// @brief Full message definition text, including dependencies.
  std::string text;
};

namespace schema {

/// @brief Render a ROS message schema using rosbag2 when available and the bundled fallback otherwise.
/// @param topic_type ROS message type in `pkg/msg/Type` form.
/// @return The schema, or `std::nullopt` when the type cannot be resolved.
std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type);

} // namespace schema
} // namespace ros_portal
