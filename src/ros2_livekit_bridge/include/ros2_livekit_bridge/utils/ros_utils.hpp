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
#include <string>
#include <vector>

#include <livekit/video_frame.h>
#include <rclcpp/logger.hpp>

namespace livekit::ros_bridge::utils
{

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(
  int width, int height,
  const std::uint8_t *rgba,
  std::size_t rgba_size);

std::string resolveEnvironmentCredential(
  const std::string & env_var_name,
  std::string & source);

void logPatternCompileErrors(
  const std::vector<PatternCompileError> & errors,
  rclcpp::Logger logger);

std::optional<ros2_livekit_bridge_config::BridgeConfig>
parseBridgeConfig(const std::filesystem::path & path, rclcpp::Logger logger);

std::vector<std::string>
outgoingTopicPatterns(
  const ros2_livekit_bridge_config::BridgeConfig & config);

} // namespace livekit::ros_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__ROS_UTILS_HPP_
