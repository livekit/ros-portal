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

#include "ros2_livekit_bridge/utils/ros_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <rclcpp/rclcpp.hpp>

namespace ros2_livekit_bridge::utils {

namespace bridge_config = ::ros2_livekit_bridge_config;

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(int width, int height, const std::uint8_t* rgba,
                                                      std::size_t rgba_size) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const std::size_t expected_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  if (rgba_size != expected_size) {
    return std::nullopt;
  }
  if (rgba == nullptr) {
    return std::nullopt;
  }

  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::RGBA);
  std::memcpy(frame.data(), rgba, rgba_size);
  return frame;
}

std::string resolveEnvironmentCredential(const std::string& env_var_name, std::string& source) {
  const char* env_val = std::getenv(env_var_name.c_str());
  if (env_val && env_val[0] != '\0') {
    source = "environment variable " + env_var_name;
    return std::string(env_val);
  }
  source = "none";
  return {};
}

std::optional<std::string> normalizeTrackTopicName(const std::string& track_name) {
  if (track_name.empty()) {
    return std::nullopt;
  }
  if (track_name.front() == '/') {
    return track_name;
  }
  return "/" + track_name;
}

std::optional<std::string> sanitizeRosNameToken(const std::string& token) {
  std::string sanitized;
  sanitized.reserve(token.size());
  for (const unsigned char ch : token) {
    if (std::isalnum(ch) || ch == '_') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return std::nullopt;
  }
  if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
    sanitized.insert(sanitized.begin(), '_');
  }
  return sanitized;
}

std::optional<std::string> liveKitToRosTopicName(const std::string& participant_identity,
                                                 const std::string& track_name) {
  if (participant_identity.empty()) {
    return std::nullopt;
  }

  const auto normalized_track_name = normalizeTrackTopicName(track_name);
  if (!normalized_track_name.has_value() || *normalized_track_name == "/") {
    return std::nullopt;
  }

  const auto participant_prefix = sanitizeRosNameToken(participant_identity);
  if (!participant_prefix.has_value()) {
    return std::nullopt;
  }
  return "/" + *participant_prefix + *normalized_track_name;
}

void logPatternCompileErrors(const std::vector<PatternCompileError>& errors, rclcpp::Logger logger) {
  for (const auto& error : errors) {
    RCLCPP_ERROR(logger, "Invalid regex pattern '%s': %s", error.pattern.c_str(), error.message.c_str());
  }
}

std::optional<bridge_config::BridgeConfig> parseBridgeConfig(const std::filesystem::path& path, rclcpp::Logger logger) {
  if (path.empty()) {
    RCLCPP_FATAL(logger, "config_path parameter is empty");
    return std::nullopt;
  }

  try {
    return bridge_config::ConfigParser{}.parseFile(path);
  } catch (const std::exception& e) {
    RCLCPP_FATAL(logger, "Failed to parse config '%s': %s", path.string().c_str(), e.what());
    return std::nullopt;
  }
}

std::vector<std::string> outgoingTopicPatterns(const bridge_config::BridgeConfig& config) {
  std::vector<std::string> patterns;
  patterns.reserve(config.topics.size());

  for (const auto& topic_config : config.topics) {
    if (topic_config.direction == bridge_config::Direction::Out ||
        topic_config.direction == bridge_config::Direction::Bidirectional) {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

std::vector<std::string> incomingTopicPatterns(const bridge_config::BridgeConfig& config) {
  std::vector<std::string> patterns;
  patterns.reserve(config.topics.size());

  for (const auto& topic_config : config.topics) {
    if (topic_config.direction == bridge_config::Direction::In ||
        topic_config.direction == bridge_config::Direction::Bidirectional) {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

std::unordered_map<std::string, std::string> incomingTopicTypes(const bridge_config::BridgeConfig& config) {
  // TODO(BOT-301): Temporary stopgap. Remove once LiveKit DataTracks carry the
  // ROS message type so inbound track types no longer need hand-configuration.
  std::unordered_map<std::string, std::string> types;

  for (const auto& topic_config : config.topics) {
    const bool inbound = topic_config.direction == bridge_config::Direction::In ||
                         topic_config.direction == bridge_config::Direction::Bidirectional;
    if (!inbound || !topic_config.msg_type.has_value()) {
      continue;
    }

    const auto normalized_topic_name = normalizeTrackTopicName(topic_config.topic);
    if (!normalized_topic_name.has_value()) {
      continue;
    }

    types.emplace(*normalized_topic_name, *topic_config.msg_type);
  }

  return types;
}
} // namespace ros2_livekit_bridge::utils
