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

#include "ros_portal/utils/config_mapping.hpp"

#include <regex>

#include "ros_portal/utils/ros_utils.hpp"
#include "ros_portal/utils/topic_matcher.hpp"

namespace ros_portal::utils {

namespace {

/// @brief Compile @p patterns, logging any invalid regexes via @p logger.
std::vector<std::regex> compileAndLog(const std::vector<std::string>& patterns, rclcpp::Logger logger) {
  std::vector<PatternCompileError> errors;
  auto compiled = compileRegexPatterns(patterns, &errors);
  logPatternCompileErrors(errors, logger);
  return compiled;
}

/// @brief Map a config encoding onto the forwarder's outbound encoding enum.
OutboundEncoding toOutboundEncoding(ros_portal_config::Encoding encoding) {
  switch (encoding) {
    case ros_portal_config::Encoding::Ros2idl:
      return OutboundEncoding::Ros2Idl;
    case ros_portal_config::Encoding::Jsonschema:
      return OutboundEncoding::JsonSchema;
    case ros_portal_config::Encoding::Ros2msg:
    default:
      return OutboundEncoding::Ros2Msg;
  }
}

/// @brief Collect per-topic outbound encodings for outbound/bidirectional
/// topics, keyed by literal ROS topic name (mirrors config `encoding`).
std::unordered_map<std::string, OutboundEncoding> outboundEncodings(
    const std::vector<ros_portal_config::TopicConfig>& topics) {
  std::unordered_map<std::string, OutboundEncoding> encodings;
  for (const auto& topic_config : topics) {
    const bool outbound = topic_config.direction == ros_portal_config::Direction::Out ||
                          topic_config.direction == ros_portal_config::Direction::Bidirectional;
    if (!outbound) {
      continue;
    }
    encodings.emplace(topic_config.topic, toOutboundEncoding(topic_config.encoding));
  }
  return encodings;
}

} // namespace

TopicForwarder::Options topicForwarderOptions(const std::vector<ros_portal_config::TopicConfig>& topics,
                                              std::size_t min_qos_depth, std::size_t max_qos_depth,
                                              const std::vector<std::string>& best_effort_qos_topics,
                                              rclcpp::Logger logger) {
  TopicForwarder::Options options;
  options.outgoing_topic_patterns = compileAndLog(outgoingTopicPatterns(topics), logger);
  options.incoming_topic_patterns = compileAndLog(incomingTopicPatterns(topics), logger);
  options.preserve_id_topic_patterns = compileAndLog(preserveIdTopicPatterns(topics), logger);
  options.best_effort_qos_topic_patterns = compileAndLog(best_effort_qos_topics, logger);
  options.min_qos_depth = min_qos_depth;
  options.max_qos_depth = max_qos_depth;
  options.outbound_rate_limits = outboundRateLimits(topics);
  options.outbound_encodings = outboundEncodings(topics);
  return options;
}

std::vector<ServiceForwarder::ServiceRoute> outgoingServiceRoutes(
    const std::vector<ros_portal_config::ServiceConfig>& services) {
  std::vector<ServiceForwarder::ServiceRoute> routes;
  routes.reserve(services.size());
  for (const auto& service_config : services) {
    if (service_config.direction != ros_portal_config::Direction::Out) {
      continue;
    }
    routes.push_back(ServiceForwarder::ServiceRoute{
        service_config.service,
        service_config.msg_type,
        service_config.participant,
    });
  }
  return routes;
}

LatchedTopicForwarder::Options latchedTopicForwarderOptions(const std::vector<ros_portal_config::TopicConfig>& topics) {
  LatchedTopicForwarder::Options options;
  options.outbound_topics = latchedOutboundTopics(topics);
  options.inbound_topics = latchedInboundTopics(topics);
  return options;
}

} // namespace ros_portal::utils
