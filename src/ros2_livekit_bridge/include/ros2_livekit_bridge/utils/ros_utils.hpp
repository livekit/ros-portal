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

#ifndef ROS2_LIVEKIT_BRIDGE__UTILS__ROS_UTILS_HPP_
#define ROS2_LIVEKIT_BRIDGE__UTILS__ROS_UTILS_HPP_

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"
#include "ros2_livekit_bridge_config/config/config_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

#include <livekit/video_frame.h>
#include <rclcpp/logger.hpp>

namespace ros2_livekit_bridge::utils
{

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(
  int width, int height,
  const std::uint8_t *rgba,
  std::size_t rgba_size);

std::string resolveEnvironmentCredential(
  const std::string & env_var_name,
  std::string & source);

/// @brief Normalize a LiveKit track name into an absolute ROS topic path.
///
/// Returns std::nullopt for empty input, preserves names that already start
/// with '/', and prefixes '/' for all other names.
///
/// Examples:
/// - "camera/image" -> "/camera/image"
/// - "/camera/image" -> "/camera/image"
/// - "" -> std::nullopt
///
/// @param track_name LiveKit track name to normalize.
/// @return Normalized ROS topic path, or std::nullopt when @p track_name is
/// empty.
std::optional<std::string> normalizeTrackTopicName(const std::string & track_name);

/// @brief Convert an arbitrary identity token into a ROS-safe name token.
///
/// Keeps ASCII alphanumeric and '_' characters, replaces all other characters
/// with '_', and prefixes '_' when the first character is a digit.
///
/// Examples:
/// - "bridge_test_a" -> "bridge_test_a"
/// - "bridge-test.a" -> "bridge_test_a"
/// - "1robot" -> "_1robot"
/// - "" -> std::nullopt
///
/// @param token Participant identity or arbitrary token.
/// @return Sanitized token, or std::nullopt when @p token is empty.
std::optional<std::string> sanitizeRosNameToken(const std::string & token);

std::optional<std::string> liveKitToRosTopicName(
  const std::string & participant_identity,
  const std::string & track_name);

void logPatternCompileErrors(
  const std::vector<PatternCompileError> & errors,
  rclcpp::Logger logger);

std::optional<ros2_livekit_bridge_config::BridgeConfig>
parseBridgeConfig(const std::filesystem::path & path, rclcpp::Logger logger);

std::vector<std::string>
outgoingTopicPatterns(
  const ros2_livekit_bridge_config::BridgeConfig & config);

std::vector<std::string>
incomingTopicPatterns(
  const ros2_livekit_bridge_config::BridgeConfig & config);
} // namespace ros2_livekit_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__ROS_UTILS_HPP_
