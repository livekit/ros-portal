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

#include "ros2_livekit_bridge/schema/renderer.hpp"

namespace ros2_livekit_bridge::schema::detail {

/// @brief Renderer backend exposed internally for comparison tests.
enum class RendererBackend {
  /// @brief Use the bundled fallback.
  Bundled,
  /// @brief Use rosbag2, returning `std::nullopt` when unavailable.
  Rosbag2,
};

/// @brief Render with a specific backend.
std::optional<RosMessageSchema> renderWithBackend(const std::string& topic_type, RendererBackend backend);

} // namespace ros2_livekit_bridge::schema::detail
