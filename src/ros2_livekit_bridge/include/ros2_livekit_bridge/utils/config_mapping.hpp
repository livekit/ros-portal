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

#include <cstddef>
#include <rclcpp/logger.hpp>
#include <string>
#include <vector>

#include "ros2_livekit_bridge/latched_topic_forwarder.hpp"
#include "ros2_livekit_bridge/service_forwarder.hpp"
#include "ros2_livekit_bridge/topic_forwarder.hpp"
#include "ros2_livekit_bridge_config/config/config_parser.hpp"

namespace ros2_livekit_bridge::utils {

/// @brief Build TopicForwarder options from the configured topics.
///
/// Distills and compiles the outgoing/incoming/preserve-id topic patterns and QoS ars
/// @ref ros2_livekit_bridge_config.
/// @param topics Configured topics (latched topics are excluded from the
/// DataTrack patterns internally).
/// @param min_qos_depth Minimum subscription history depth (from ROS params).
/// @param max_qos_depth Maximum subscription history depth (from ROS params).
/// @param best_effort_qos_topics Raw best-effort QoS topic patterns (from ROS
/// params) compiled here.
/// @param logger Logger used to report invalid regex patterns.
TopicForwarder::Options topicForwarderOptions(const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics,
                                              std::size_t min_qos_depth, std::size_t max_qos_depth,
                                              const std::vector<std::string>& best_effort_qos_topics,
                                              rclcpp::Logger logger);

/// @brief Build outbound ServiceForwarder routes from the configured services.
/// @param services Configured services; only `Direction::Out` entries yield a
/// route.
std::vector<ServiceForwarder::ServiceRoute> outgoingServiceRoutes(
    const std::vector<ros2_livekit_bridge_config::ServiceConfig>& services);

/// @brief Build LatchedTopicForwarder options from the configured topics.
///
/// Collects the outbound (literal) and inbound (normalized) latched topic sets;
/// tunables are left at their @ref LatchedTopicForwarder::Options defaults.
/// @param topics Configured topics; only entries flagged `latched` contribute.
LatchedTopicForwarder::Options latchedTopicForwarderOptions(
    const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics);

} // namespace ros2_livekit_bridge::utils
