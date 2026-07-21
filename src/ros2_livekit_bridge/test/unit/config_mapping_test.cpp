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

#include "ros2_livekit_bridge/utils/config_mapping.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <rclcpp/logger.hpp>
#include <string>
#include <vector>

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

namespace ros2_livekit_bridge::utils {
namespace {

namespace bc = ::ros2_livekit_bridge_config;

bc::TopicConfig makeTopic(const std::string& topic, bc::Direction direction) {
  bc::TopicConfig config;
  config.topic = topic;
  config.direction = direction;
  return config;
}

rclcpp::Logger testLogger() { return rclcpp::get_logger("config_mapping_test"); }

TEST(ConfigMappingTest, TopicForwarderOptionsCompilesPatternsAndWiresQos) {
  std::vector<bc::TopicConfig> topics;
  topics.push_back(makeTopic("/camera/image", bc::Direction::Out));
  topics.push_back(makeTopic("/teleop_cmd", bc::Direction::In));

  bc::TopicConfig rate_capped = makeTopic("/odom", bc::Direction::Bidirectional);
  rate_capped.max_rate_hz = 10.0;
  topics.push_back(rate_capped);

  const auto options = topicForwarderOptions(topics, /*min_qos_depth=*/2, /*max_qos_depth=*/8,
                                             /*best_effort_qos_topics=*/{"/camera/.*"}, testLogger());

  // Outgoing = out + bidirectional; incoming = in + bidirectional.
  EXPECT_EQ(options.outgoing_topic_patterns.size(), 2U);
  EXPECT_EQ(options.incoming_topic_patterns.size(), 2U);
  EXPECT_EQ(options.best_effort_qos_topic_patterns.size(), 1U);
  EXPECT_EQ(options.min_qos_depth, 2U);
  EXPECT_EQ(options.max_qos_depth, 8U);

  EXPECT_TRUE(matchesAnyPattern("/camera/image", options.outgoing_topic_patterns));
  EXPECT_TRUE(matchesAnyPattern("/odom", options.outgoing_topic_patterns));
  EXPECT_TRUE(matchesAnyPattern("/odom", options.incoming_topic_patterns));
  EXPECT_TRUE(matchesAnyPattern("/camera/image", options.best_effort_qos_topic_patterns));

  ASSERT_EQ(options.outbound_rate_limits.count("/odom"), 1U);
  EXPECT_DOUBLE_EQ(options.outbound_rate_limits.at("/odom"), 10.0);
}

TEST(ConfigMappingTest, TopicForwarderOptionsRoutesPreserveId) {
  bc::TopicConfig topic = makeTopic("/remote/state", bc::Direction::In);
  topic.preserve_id = true;

  const auto options = topicForwarderOptions({topic}, /*min_qos_depth=*/1, /*max_qos_depth=*/10,
                                             /*best_effort_qos_topics=*/{}, testLogger());

  EXPECT_TRUE(matchesAnyPattern("/remote/state", options.preserve_id_topic_patterns));
}

TEST(ConfigMappingTest, LatchedTopicsAreSplitOffFromDataTrackPatterns) {
  bc::TopicConfig latched = makeTopic("/tf_static", bc::Direction::Out);
  latched.latched = true;

  const auto topics = std::vector<bc::TopicConfig>{latched, makeTopic("/tf", bc::Direction::Out)};

  const auto topic_options = topicForwarderOptions(topics, 1, 10, {}, testLogger());
  const auto latched_options = latchedTopicForwarderOptions(topics);

  // Latched topic is handled over RPC, not on the DataTrack path.
  EXPECT_FALSE(matchesAnyPattern("/tf_static", topic_options.outgoing_topic_patterns));
  EXPECT_TRUE(matchesAnyPattern("/tf", topic_options.outgoing_topic_patterns));
  EXPECT_EQ(latched_options.outbound_topics.count("/tf_static"), 1U);
  EXPECT_TRUE(latched_options.inbound_topics.empty());
}

TEST(ConfigMappingTest, LatchedInboundTopicsAreNormalized) {
  bc::TopicConfig latched = makeTopic("tf_static", bc::Direction::In); // no leading slash
  latched.latched = true;

  const auto latched_options = latchedTopicForwarderOptions({latched});

  EXPECT_EQ(latched_options.inbound_topics.count("/tf_static"), 1U);
  EXPECT_TRUE(latched_options.outbound_topics.empty());
}

TEST(ConfigMappingTest, OutgoingServiceRoutesKeepsOnlyOutDirection) {
  std::vector<bc::ServiceConfig> services;

  bc::ServiceConfig out_service;
  out_service.service = "/set_bool";
  out_service.direction = bc::Direction::Out;
  out_service.participant = "robot-b";
  out_service.msg_type = "std_srvs/srv/SetBool";
  services.push_back(out_service);

  bc::ServiceConfig in_service;
  in_service.service = "/get_state";
  in_service.direction = bc::Direction::In;
  in_service.participant = "robot-c";
  in_service.msg_type = "std_srvs/srv/Trigger";
  services.push_back(in_service);

  const auto routes = outgoingServiceRoutes(services);

  ASSERT_EQ(routes.size(), 1U);
  EXPECT_EQ(routes.front().service, "/set_bool");
  EXPECT_EQ(routes.front().msg_type, "std_srvs/srv/SetBool");
  EXPECT_EQ(routes.front().participant, "robot-b");
}

} // namespace
} // namespace ros2_livekit_bridge::utils
