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

#include <gtest/gtest.h>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <rclcpp/logger.hpp>
#include <string>
#include <unordered_set>
#include <vector>

#include "ros_portal_config/config/config_parser.hpp"
#include "test_common.hpp"

namespace ros_portal::utils {
namespace {

constexpr const char* kTestEnvVar = "ROS_PORTAL_TEST_CREDENTIAL";

using ros_portal::test::ScopedEnvVar;

TEST(RosUtilsTest, MakeRgbaVideoFrameCopiesMatchingRgbaBuffer) {
  const std::vector<std::uint8_t> rgba = {10, 20, 30, 40, 50, 60, 70, 80};

  auto frame = makeRgbaVideoFrame(2, 1, rgba.data(), rgba.size());

  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(std::memcmp(frame->data(), rgba.data(), rgba.size()), 0);
}

TEST(RosUtilsTest, MakeRgbaVideoFrameReturnsEmptyForWrongSize) {
  const std::vector<std::uint8_t> rgba = {10, 20, 30, 40};

  const auto frame = makeRgbaVideoFrame(2, 1, rgba.data(), rgba.size());

  EXPECT_FALSE(frame.has_value());
}

TEST(RosUtilsTest, MakeRgbaVideoFrameReturnsEmptyForNonPositiveDimensions) {
  const std::vector<std::uint8_t> rgba = {10, 20, 30, 40};

  EXPECT_FALSE(makeRgbaVideoFrame(0, 1, rgba.data(), rgba.size()).has_value());
  EXPECT_FALSE(makeRgbaVideoFrame(1, -1, rgba.data(), rgba.size()).has_value());
}

TEST(RosUtilsTest, MakeRgbaVideoFrameReturnsEmptyForNullBuffer) {
  EXPECT_FALSE(makeRgbaVideoFrame(1, 1, nullptr, 4).has_value());
}

TEST(RosUtilsTest, EnvironmentVariableReadsNonEmptyValue) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  setenv(kTestEnvVar, "secret-token", 1);

  const auto value = environmentVariable(kTestEnvVar);

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "secret-token");
}

TEST(RosUtilsTest, EnvironmentVariableTreatsMissingValueAsNone) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  unsetenv(kTestEnvVar);

  const auto value = environmentVariable(kTestEnvVar);

  EXPECT_FALSE(value.has_value());
}

TEST(RosUtilsTest, EnvironmentVariableTreatsEmptyValueAsNone) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  setenv(kTestEnvVar, "", 1);

  const auto value = environmentVariable(kTestEnvVar);

  EXPECT_FALSE(value.has_value());
}

TEST(RosUtilsTest, DefaultConfigForwardsAllTopicsBidirectionally) {
  const auto config = parseRosPortalConfig(std::filesystem::path{}, rclcpp::get_logger("ros_utils_test"));

  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->topic_polling_period_ms, 500);
  ASSERT_EQ(config->topics.size(), 1u);
  EXPECT_EQ(config->topics.front().topic, ".*");
  EXPECT_EQ(config->topics.front().direction, ros_portal_config::Direction::Bidirectional);
}

TEST(RosUtilsTest, BuiltinDefaultConfigMatchesInstalledAllTopicsYaml) {
  const auto builtin = parseRosPortalConfig(std::filesystem::path{}, rclcpp::get_logger("ros_utils_test"));
  ASSERT_TRUE(builtin.has_value());

  const auto share_dir = ament_index_cpp::get_package_share_directory("ros_portal");
  const auto all_topics_path = std::filesystem::path(share_dir) / "config" / "all_topics.yaml";
  ASSERT_TRUE(std::filesystem::exists(all_topics_path)) << all_topics_path.string();

  const auto from_file = parseRosPortalConfig(all_topics_path, rclcpp::get_logger("ros_utils_test"));
  ASSERT_TRUE(from_file.has_value());

  EXPECT_EQ(builtin->version, from_file->version);
  EXPECT_EQ(builtin->topic_polling_period_ms, from_file->topic_polling_period_ms);
  EXPECT_EQ(builtin->ros_threads, from_file->ros_threads);
  ASSERT_EQ(builtin->services.size(), from_file->services.size());
  ASSERT_EQ(builtin->topics.size(), from_file->topics.size());
  for (std::size_t i = 0; i < builtin->topics.size(); ++i) {
    EXPECT_EQ(builtin->topics[i].topic, from_file->topics[i].topic);
    EXPECT_EQ(builtin->topics[i].direction, from_file->topics[i].direction);
  }
}

TEST(RosUtilsTest, OutgoingTopicPatternsIncludesOutAndBidirectionalTopics) {
  ros_portal_config::RosPortalConfig config;
  ros_portal_config::TopicConfig out_topic;
  out_topic.topic = "/camera/image_raw";
  out_topic.direction = ros_portal_config::Direction::Out;
  config.topics.push_back(out_topic);

  ros_portal_config::TopicConfig in_topic;
  in_topic.topic = "/teleop_cmd";
  in_topic.direction = ros_portal_config::Direction::In;
  config.topics.push_back(in_topic);

  ros_portal_config::TopicConfig bidirectional_topic;
  bidirectional_topic.topic = "/odom";
  bidirectional_topic.direction = ros_portal_config::Direction::Bidirectional;
  config.topics.push_back(bidirectional_topic);

  const auto patterns = outgoingTopicPatterns(config.topics);

  EXPECT_EQ(patterns, (std::vector<std::string>{"/camera/image_raw", "/odom"}));
}

TEST(RosUtilsTest, NormalizeTrackTopicNameAddsLeadingSlash) {
  const auto normalized = normalizeTrackTopicName("camera/image");
  ASSERT_TRUE(normalized.has_value());
  EXPECT_EQ(*normalized, "/camera/image");
}

TEST(RosUtilsTest, NormalizeTrackTopicNamePreservesExistingLeadingSlash) {
  const auto normalized = normalizeTrackTopicName("/camera/image");
  ASSERT_TRUE(normalized.has_value());
  EXPECT_EQ(*normalized, "/camera/image");
}

TEST(RosUtilsTest, NormalizeTrackTopicNameReturnsEmptyForEmptyInput) {
  EXPECT_FALSE(normalizeTrackTopicName("").has_value());
}

TEST(RosUtilsTest, SanitizeRosNameTokenKeepsValidCharacters) {
  const auto sanitized = sanitizeRosNameToken("ros_portal_test_a");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(*sanitized, "ros_portal_test_a");
}

TEST(RosUtilsTest, SanitizeRosNameTokenReplacesInvalidCharacters) {
  const auto sanitized = sanitizeRosNameToken("ros-portal-test.a");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(*sanitized, "ros_portal_test_a");
}

TEST(RosUtilsTest, SanitizeRosNameTokenReturnsEmptyForEmptyInput) {
  EXPECT_FALSE(sanitizeRosNameToken("").has_value());
}

TEST(RosUtilsTest, SanitizeRosNameTokenPrefixesLeadingDigit) {
  const auto sanitized = sanitizeRosNameToken("1robot");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(*sanitized, "_1robot");
}

TEST(RosUtilsTest, LiveKitToRosTopicNameNormalizesTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("ros_portal/out");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/ros_portal/out");
}

TEST(RosUtilsTest, LiveKitToRosTopicNamePreservesLeadingSlashInTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("/ros_portal/out");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/ros_portal/out");
}

TEST(RosUtilsTest, LiveKitToRosTopicNameReturnsEmptyForEmptyTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("");
  EXPECT_FALSE(ros_topic_name.has_value());
}

TEST(RosUtilsTest, LiveKitToRosTopicNameReturnsEmptyForRootTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("/");

  EXPECT_FALSE(ros_topic_name.has_value());
}

TEST(RosUtilsTest, LiveKitToRosTopicNamePrefixesSanitizedParticipantIdentity) {
  const auto ros_topic_name = liveKitToRosTopicName("robot-1", "/tf");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/robot_1/tf");
}

TEST(RosUtilsTest, LiveKitToRosTopicNameWithIdentityNormalizesTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("robot_a", "tf");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/robot_a/tf");
}

TEST(RosUtilsTest, LiveKitToRosTopicNameReturnsEmptyForEmptyIdentity) {
  EXPECT_FALSE(liveKitToRosTopicName("", "/tf").has_value());
}

TEST(RosUtilsTest, LiveKitToRosTopicNameWithIdentityReturnsEmptyForRootTrackName) {
  EXPECT_FALSE(liveKitToRosTopicName("robot-1", "/").has_value());
}

TEST(RosUtilsTest, PreserveIdTopicPatternsIncludesOnlyInboundOptedInTopics) {
  ros_portal_config::RosPortalConfig config;

  ros_portal_config::TopicConfig in_preserve;
  in_preserve.topic = "/tf";
  in_preserve.direction = ros_portal_config::Direction::In;
  in_preserve.preserve_id = true;
  config.topics.push_back(in_preserve);

  ros_portal_config::TopicConfig bidirectional_preserve;
  bidirectional_preserve.topic = "/odom";
  bidirectional_preserve.direction = ros_portal_config::Direction::Bidirectional;
  bidirectional_preserve.preserve_id = true;
  config.topics.push_back(bidirectional_preserve);

  ros_portal_config::TopicConfig in_no_preserve;
  in_no_preserve.topic = "/teleop_cmd";
  in_no_preserve.direction = ros_portal_config::Direction::In;
  config.topics.push_back(in_no_preserve);

  ros_portal_config::TopicConfig out_preserve;
  out_preserve.topic = "/camera/image_raw";
  out_preserve.direction = ros_portal_config::Direction::Out;
  out_preserve.preserve_id = true;
  config.topics.push_back(out_preserve);

  const auto patterns = preserveIdTopicPatterns(config.topics);

  EXPECT_EQ(patterns, (std::vector<std::string>{"/tf", "/odom"}));
}

TEST(RosUtilsTest, IncomingTopicPatternsIncludesInAndBidirectionalTopics) {
  ros_portal_config::RosPortalConfig config;
  ros_portal_config::TopicConfig out_topic;
  out_topic.topic = "/camera/image_raw";
  out_topic.direction = ros_portal_config::Direction::Out;
  config.topics.push_back(out_topic);

  ros_portal_config::TopicConfig in_topic;
  in_topic.topic = "/teleop_cmd";
  in_topic.direction = ros_portal_config::Direction::In;
  config.topics.push_back(in_topic);

  ros_portal_config::TopicConfig bidirectional_topic;
  bidirectional_topic.topic = "/odom";
  bidirectional_topic.direction = ros_portal_config::Direction::Bidirectional;
  config.topics.push_back(bidirectional_topic);

  const auto patterns = incomingTopicPatterns(config.topics);

  EXPECT_EQ(patterns, (std::vector<std::string>{"/teleop_cmd", "/odom"}));
}

TEST(RosUtilsTest, OutboundRateLimitsMapsOutboundTopicsOnly) {
  ros_portal_config::RosPortalConfig config;

  ros_portal_config::TopicConfig out_limited;
  out_limited.topic = "/tf";
  out_limited.direction = ros_portal_config::Direction::Out;
  out_limited.max_rate_hz = 10.0;
  config.topics.push_back(out_limited);

  ros_portal_config::TopicConfig bidirectional_limited;
  bidirectional_limited.topic = "/odom";
  bidirectional_limited.direction = ros_portal_config::Direction::Bidirectional;
  bidirectional_limited.max_rate_hz = 5.0;
  config.topics.push_back(bidirectional_limited);

  // Inbound topic: excluded even with a rate set.
  ros_portal_config::TopicConfig in_limited;
  in_limited.topic = "/teleop_cmd";
  in_limited.direction = ros_portal_config::Direction::In;
  in_limited.max_rate_hz = 20.0;
  config.topics.push_back(in_limited);

  // Outbound topic without a cap: excluded.
  ros_portal_config::TopicConfig out_unlimited;
  out_unlimited.topic = "/camera/image_raw";
  out_unlimited.direction = ros_portal_config::Direction::Out;
  config.topics.push_back(out_unlimited);

  // Non-positive cap: excluded.
  ros_portal_config::TopicConfig out_zero;
  out_zero.topic = "/scan";
  out_zero.direction = ros_portal_config::Direction::Out;
  out_zero.max_rate_hz = 0.0;
  config.topics.push_back(out_zero);

  const auto limits = outboundRateLimits(config.topics);

  EXPECT_EQ(limits, (std::unordered_map<std::string, double>{{"/tf", 10.0}, {"/odom", 5.0}}));
}

TEST(RosUtilsTest, LatchedTopicsAreExcludedFromDataTrackPatterns) {
  ros_portal_config::RosPortalConfig config;

  ros_portal_config::TopicConfig out_latched;
  out_latched.topic = "/tf_static";
  out_latched.direction = ros_portal_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  ros_portal_config::TopicConfig out_plain;
  out_plain.topic = "/tf";
  out_plain.direction = ros_portal_config::Direction::Out;
  config.topics.push_back(out_plain);

  ros_portal_config::TopicConfig in_latched;
  in_latched.topic = "/robot_description";
  in_latched.direction = ros_portal_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  ros_portal_config::TopicConfig in_plain;
  in_plain.topic = "/map";
  in_plain.direction = ros_portal_config::Direction::In;
  config.topics.push_back(in_plain);

  // Latched topics are handled by LatchedTopicForwarder, so they must not appear
  // in the DataTrack forwarding patterns.
  EXPECT_EQ(outgoingTopicPatterns(config.topics), (std::vector<std::string>{"/tf"}));
  EXPECT_EQ(incomingTopicPatterns(config.topics), (std::vector<std::string>{"/map"}));
}

TEST(RosUtilsTest, LatchedOutboundTopicsCollectsOutboundLatchedOnly) {
  ros_portal_config::RosPortalConfig config;

  ros_portal_config::TopicConfig out_latched;
  out_latched.topic = "/tf_static";
  out_latched.direction = ros_portal_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  ros_portal_config::TopicConfig bidirectional_latched;
  bidirectional_latched.topic = "/params";
  bidirectional_latched.direction = ros_portal_config::Direction::Bidirectional;
  bidirectional_latched.latched = true;
  config.topics.push_back(bidirectional_latched);

  // Inbound-only latched: excluded from the outbound set.
  ros_portal_config::TopicConfig in_latched;
  in_latched.topic = "/robot_description";
  in_latched.direction = ros_portal_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  // Outbound but not latched: excluded.
  ros_portal_config::TopicConfig out_plain;
  out_plain.topic = "/tf";
  out_plain.direction = ros_portal_config::Direction::Out;
  config.topics.push_back(out_plain);

  EXPECT_EQ(latchedOutboundTopics(config.topics), (std::unordered_set<std::string>{"/tf_static", "/params"}));
}

TEST(RosUtilsTest, LatchedInboundTopicsNormalizesInboundLatchedOnly) {
  ros_portal_config::RosPortalConfig config;

  ros_portal_config::TopicConfig in_latched;
  in_latched.topic = "tf_static"; // no leading slash -> normalized
  in_latched.direction = ros_portal_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  ros_portal_config::TopicConfig bidirectional_latched;
  bidirectional_latched.topic = "/params";
  bidirectional_latched.direction = ros_portal_config::Direction::Bidirectional;
  bidirectional_latched.latched = true;
  config.topics.push_back(bidirectional_latched);

  // Outbound-only latched: excluded from the inbound set.
  ros_portal_config::TopicConfig out_latched;
  out_latched.topic = "/odom_static";
  out_latched.direction = ros_portal_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  EXPECT_EQ(latchedInboundTopics(config.topics), (std::unordered_set<std::string>{"/tf_static", "/params"}));
}
} // namespace
} // namespace ros_portal::utils
