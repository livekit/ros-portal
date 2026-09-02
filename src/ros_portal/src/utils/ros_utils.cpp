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

#include "ros_portal/utils/ros_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <rclcpp/rclcpp.hpp>

namespace ros_portal::utils {

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

std::optional<std::string> environmentVariable(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string{value};
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

std::optional<std::string> liveKitToRosTopicName(const std::string& track_name) {
  auto normalized_track_name = normalizeTrackTopicName(track_name);
  if (!normalized_track_name.has_value() || *normalized_track_name == "/") {
    return std::nullopt;
  }
  return normalized_track_name;
}

std::optional<std::string> liveKitToRosTopicName(const std::string& participant_identity,
                                                 const std::string& track_name) {
  const auto ros_topic_name = liveKitToRosTopicName(track_name);
  if (!ros_topic_name.has_value()) {
    return std::nullopt;
  }

  const auto participant_prefix = sanitizeRosNameToken(participant_identity);
  if (!participant_prefix.has_value()) {
    return std::nullopt;
  }
  return "/" + *participant_prefix + *ros_topic_name;
}

void logPatternCompileErrors(const std::vector<PatternCompileError>& errors, const rclcpp::Logger& logger) {
  for (const auto& error : errors) {
    RCLCPP_ERROR(logger, "Invalid regex pattern '%s': %s", error.pattern.c_str(), error.message.c_str());
  }
}

std::optional<ros_portal_config::RosPortalConfig> parseRosPortalConfig(const std::filesystem::path& path,
                                                                       const rclcpp::Logger& logger) {
  if (path.empty()) {
    RCLCPP_INFO(logger,
                "No config_path provided; using builtin default config that forwards all topics bidirectionally");
    try {
      return ros_portal_config::ConfigParser{}.parseString(kDefaultConfigYaml);
    } catch (const std::exception& e) {
      RCLCPP_FATAL(logger, "Failed to parse builtin default config: %s", e.what());
      return std::nullopt;
    }
  }

  try {
    return ros_portal_config::ConfigParser{}.parseFile(path);
  } catch (const std::exception& e) {
    RCLCPP_FATAL(logger, "Failed to parse config '%s': %s", path.string().c_str(), e.what());
    return std::nullopt;
  }
}

std::vector<std::string> outgoingTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::vector<std::string> patterns;
  patterns.reserve(topics.size());

  for (const auto& topic_config : topics) {
    if (topic_config.latched) {
      continue; // handled by LatchedTopicForwarder, not the DataTrack path
    }
    if (topic_config.direction == ros_portal_config::Direction::Out ||
        topic_config.direction == ros_portal_config::Direction::Bidirectional) {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

std::vector<std::string> incomingTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::vector<std::string> patterns;
  patterns.reserve(topics.size());

  for (const auto& topic_config : topics) {
    if (topic_config.latched) {
      continue; // handled by LatchedTopicForwarder, not the DataTrack path
    }
    if (topic_config.direction == ros_portal_config::Direction::In ||
        topic_config.direction == ros_portal_config::Direction::Bidirectional) {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

std::vector<std::string> preserveIdTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::vector<std::string> patterns;

  for (const auto& topic_config : topics) {
    const bool inbound = topic_config.direction == ros_portal_config::Direction::In ||
                         topic_config.direction == ros_portal_config::Direction::Bidirectional;
    if (inbound && topic_config.preserve_id) {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

std::unordered_map<std::string, double> outboundRateLimits(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::unordered_map<std::string, double> limits;

  for (const auto& topic_config : topics) {
    const bool outbound = topic_config.direction == ros_portal_config::Direction::Out ||
                          topic_config.direction == ros_portal_config::Direction::Bidirectional;
    if (!outbound || !topic_config.max_rate_hz.has_value() || *topic_config.max_rate_hz <= 0.0) {
      continue;
    }

    limits.emplace(topic_config.topic, *topic_config.max_rate_hz);
  }

  return limits;
}

std::unordered_set<std::string> latchedOutboundTopics(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::unordered_set<std::string> result;

  for (const auto& topic_config : topics) {
    const bool outbound = topic_config.direction == ros_portal_config::Direction::Out ||
                          topic_config.direction == ros_portal_config::Direction::Bidirectional;
    if (outbound && topic_config.latched) {
      result.insert(topic_config.topic);
    }
  }

  return result;
}

std::unordered_set<std::string> latchedInboundTopics(const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::unordered_set<std::string> result;

  for (const auto& topic_config : topics) {
    const bool inbound = topic_config.direction == ros_portal_config::Direction::In ||
                         topic_config.direction == ros_portal_config::Direction::Bidirectional;
    if (!inbound || !topic_config.latched) {
      continue;
    }

    const auto normalized_topic_name = normalizeTrackTopicName(topic_config.topic);
    if (!normalized_topic_name.has_value()) {
      continue;
    }

    result.insert(*normalized_topic_name);
  }

  return result;
}
} // namespace ros_portal::utils
