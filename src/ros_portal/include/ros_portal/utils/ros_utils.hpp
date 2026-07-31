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

#ifndef ROS_PORTAL__UTILS__ROS_UTILS_HPP_
#define ROS_PORTAL__UTILS__ROS_UTILS_HPP_

#include <livekit/video_frame.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <rclcpp/logger.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ros_portal/utils/topic_matcher.hpp"
#include "ros_portal_config/config/config_parser.hpp"

namespace ros_portal::utils {

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(int width, int height, const std::uint8_t* rgba,
                                                      std::size_t rgba_size);

std::string resolveEnvironmentCredential(const std::string& env_var_name, std::string& source);

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
std::optional<std::string> normalizeTrackTopicName(const std::string& track_name);

/// @brief Convert an arbitrary identity token into a ROS-safe name token.
///
/// Keeps ASCII alphanumeric and '_' characters, replaces all other characters
/// with '_', and prefixes '_' when the first character is a digit.
///
/// Examples:
/// - "ros_portal_test_a" -> "ros_portal_test_a"
/// - "ros-portal-test.a" -> "ros_portal_test_a"
/// - "1robot" -> "_1robot"
/// - "" -> std::nullopt
///
/// @param token Participant identity or arbitrary token.
/// @return Sanitized token, or std::nullopt when @p token is empty.
std::optional<std::string> sanitizeRosNameToken(const std::string& token);

/// @brief Convert a LiveKit data track name into the local ROS topic path.
///
/// Returns std::nullopt for empty input or a track name that normalizes to '/'.
///
/// @param track_name LiveKit data track name to convert.
/// @return Normalized ROS topic path, or std::nullopt when @p track_name is
/// invalid.
std::optional<std::string> liveKitToRosTopicName(const std::string& track_name);

/// @brief Convert a LiveKit data track name into a participant-prefixed ROS
/// topic path.
/// @param participant_identity LiveKit identity of the publishing participant.
/// @param track_name LiveKit data track name to convert.
/// @return Prefixed ROS topic path, or std::nullopt when either input is invalid.
std::optional<std::string> liveKitToRosTopicName(const std::string& participant_identity,
                                                 const std::string& track_name);

void logPatternCompileErrors(const std::vector<PatternCompileError>& errors, rclcpp::Logger logger);

std::optional<ros_portal_config::RosPortalConfig> parseRosPortalConfig(const std::filesystem::path& path,
                                                                       rclcpp::Logger logger);

/// @brief Collect ROS-to-LiveKit topic patterns for the DataTrack forwarding
/// path. Topics flagged `latched` are excluded because they are handled by the
/// LatchedTopicForwarder over RPC instead (see @ref latchedOutboundTopics).
std::vector<std::string> outgoingTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics);

/// @brief Collect LiveKit-to-ROS topic patterns for the DataTrack forwarding
/// path. Topics flagged `latched` are excluded because they are handled by the
/// LatchedTopicForwarder over RPC instead (see @ref latchedInboundTopics).
std::vector<std::string> incomingTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics);

/// @brief Collect topic patterns for inbound topics that request identity
/// prefixing.
/// @param topics The configured topics.
/// @return Vector of topic patterns for inbound topics that request identity
/// prefixing.
std::vector<std::string> preserveIdTopicPatterns(const std::vector<ros_portal_config::TopicConfig>& topics);

/// @brief Collect per-topic outbound forward-rate caps.
///
/// Maps ROS topic name -> maximum forward rate in Hz for every 'out'/'bidirectional'
/// topic that sets a positive `max_rate_hz` in the config. Keyed by the verbatim
/// configured topic name (literal match, not regex).
///
/// @param topics The configured topics.
/// @return Map of ROS topic name to maximum outbound forward rate (Hz).
std::unordered_map<std::string, double> outboundRateLimits(const std::vector<ros_portal_config::TopicConfig>& topics);

/// @brief Collect literal ROS topic names for outbound latched topics.
///
/// Every 'out'/'bidirectional' topic flagged `latched` in the config. These are
/// forwarded to peers over the RPC push-with-ack path (LatchedTopicForwarder)
/// rather than as LiveKit DataTracks. Keyed by the verbatim configured topic
/// name (literal match, not regex), mirroring `outboundRateLimits`.
///
/// @param topics The configured topics.
/// @return Set of ROS topic names to forward as latched state.
std::unordered_set<std::string> latchedOutboundTopics(const std::vector<ros_portal_config::TopicConfig>& topics);

/// @brief Collect normalized ROS topic names for inbound latched topics.
///
/// Every 'in'/'bidirectional' topic flagged `latched` in the config. ROS Portal
/// accepts a latched-state RPC only for topics in this set and republishes them
/// on a TRANSIENT_LOCAL publisher. Keyed by normalized ROS topic name, mirroring
/// `incomingTopicTypes`.
///
/// @param topics The configured topics.
/// @return Set of normalized ROS topic names accepted as inbound latched state.
std::unordered_set<std::string> latchedInboundTopics(const std::vector<ros_portal_config::TopicConfig>& topics);
} // namespace ros_portal::utils

#endif // ROS_PORTAL__UTILS__ROS_UTILS_HPP_
