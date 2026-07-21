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

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace ros2_livekit_bridge::utils {
namespace {

constexpr const char* kTestEnvVar = "ROS2_LIVEKIT_BRIDGE_TEST_CREDENTIAL";

struct ScopedEnvVar {
  explicit ScopedEnvVar(const char* name) : name(name) {
    const char* value = std::getenv(name);
    if (value) {
      had_value = true;
      original_value = value;
    }
  }

  ~ScopedEnvVar() {
    if (had_value) {
      setenv(name.c_str(), original_value.c_str(), 1);
    } else {
      unsetenv(name.c_str());
    }
  }

  std::string name;
  bool had_value{false};
  std::string original_value;
};

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

TEST(RosUtilsTest, ResolveEnvironmentCredentialReadsNonEmptyValue) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  setenv(kTestEnvVar, "secret-token", 1);
  std::string source;

  const auto value = resolveEnvironmentCredential(kTestEnvVar, source);

  EXPECT_EQ(value, "secret-token");
  EXPECT_EQ(source, std::string("environment variable ") + kTestEnvVar);
}

TEST(RosUtilsTest, ResolveEnvironmentCredentialTreatsMissingValueAsNone) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  unsetenv(kTestEnvVar);
  std::string source = "unchanged";

  const auto value = resolveEnvironmentCredential(kTestEnvVar, source);

  EXPECT_TRUE(value.empty());
  EXPECT_EQ(source, "none");
}

TEST(RosUtilsTest, ResolveEnvironmentCredentialTreatsEmptyValueAsNone) {
  ScopedEnvVar scoped_env{kTestEnvVar};
  setenv(kTestEnvVar, "", 1);
  std::string source = "unchanged";

  const auto value = resolveEnvironmentCredential(kTestEnvVar, source);

  EXPECT_TRUE(value.empty());
  EXPECT_EQ(source, "none");
}

TEST(RosUtilsTest, OutgoingTopicPatternsIncludesOutAndBidirectionalTopics) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;
  bridge_config::TopicConfig out_topic;
  out_topic.topic = "/camera/image_raw";
  out_topic.direction = bridge_config::Direction::Out;
  config.topics.push_back(out_topic);

  bridge_config::TopicConfig in_topic;
  in_topic.topic = "/teleop_cmd";
  in_topic.direction = bridge_config::Direction::In;
  config.topics.push_back(in_topic);

  bridge_config::TopicConfig bidirectional_topic;
  bidirectional_topic.topic = "/odom";
  bidirectional_topic.direction = bridge_config::Direction::Bidirectional;
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
  const auto sanitized = sanitizeRosNameToken("bridge_test_a");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(*sanitized, "bridge_test_a");
}

TEST(RosUtilsTest, SanitizeRosNameTokenReplacesInvalidCharacters) {
  const auto sanitized = sanitizeRosNameToken("bridge-test.a");
  ASSERT_TRUE(sanitized.has_value());
  EXPECT_EQ(*sanitized, "bridge_test_a");
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
  const auto ros_topic_name = liveKitToRosTopicName("bridge/out");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/bridge/out");
}

TEST(RosUtilsTest, LiveKitToRosTopicNamePreservesLeadingSlashInTrackName) {
  const auto ros_topic_name = liveKitToRosTopicName("/bridge/out");

  ASSERT_TRUE(ros_topic_name.has_value());
  EXPECT_EQ(*ros_topic_name, "/bridge/out");
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
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;

  bridge_config::TopicConfig in_preserve;
  in_preserve.topic = "/tf";
  in_preserve.direction = bridge_config::Direction::In;
  in_preserve.preserve_id = true;
  config.topics.push_back(in_preserve);

  bridge_config::TopicConfig bidirectional_preserve;
  bidirectional_preserve.topic = "/odom";
  bidirectional_preserve.direction = bridge_config::Direction::Bidirectional;
  bidirectional_preserve.preserve_id = true;
  config.topics.push_back(bidirectional_preserve);

  bridge_config::TopicConfig in_no_preserve;
  in_no_preserve.topic = "/teleop_cmd";
  in_no_preserve.direction = bridge_config::Direction::In;
  config.topics.push_back(in_no_preserve);

  bridge_config::TopicConfig out_preserve;
  out_preserve.topic = "/camera/image_raw";
  out_preserve.direction = bridge_config::Direction::Out;
  out_preserve.preserve_id = true;
  config.topics.push_back(out_preserve);

  const auto patterns = preserveIdTopicPatterns(config.topics);

  EXPECT_EQ(patterns, (std::vector<std::string>{"/tf", "/odom"}));
}

TEST(RosUtilsTest, IncomingTopicPatternsIncludesInAndBidirectionalTopics) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;
  bridge_config::TopicConfig out_topic;
  out_topic.topic = "/camera/image_raw";
  out_topic.direction = bridge_config::Direction::Out;
  config.topics.push_back(out_topic);

  bridge_config::TopicConfig in_topic;
  in_topic.topic = "/teleop_cmd";
  in_topic.direction = bridge_config::Direction::In;
  config.topics.push_back(in_topic);

  bridge_config::TopicConfig bidirectional_topic;
  bidirectional_topic.topic = "/odom";
  bidirectional_topic.direction = bridge_config::Direction::Bidirectional;
  config.topics.push_back(bidirectional_topic);

  const auto patterns = incomingTopicPatterns(config.topics);

  EXPECT_EQ(patterns, (std::vector<std::string>{"/teleop_cmd", "/odom"}));
}

TEST(RosUtilsTest, OutboundRateLimitsMapsOutboundTopicsOnly) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;

  bridge_config::TopicConfig out_limited;
  out_limited.topic = "/tf";
  out_limited.direction = bridge_config::Direction::Out;
  out_limited.max_rate_hz = 10.0;
  config.topics.push_back(out_limited);

  bridge_config::TopicConfig bidirectional_limited;
  bidirectional_limited.topic = "/odom";
  bidirectional_limited.direction = bridge_config::Direction::Bidirectional;
  bidirectional_limited.max_rate_hz = 5.0;
  config.topics.push_back(bidirectional_limited);

  // Inbound topic: excluded even with a rate set.
  bridge_config::TopicConfig in_limited;
  in_limited.topic = "/teleop_cmd";
  in_limited.direction = bridge_config::Direction::In;
  in_limited.max_rate_hz = 20.0;
  config.topics.push_back(in_limited);

  // Outbound topic without a cap: excluded.
  bridge_config::TopicConfig out_unlimited;
  out_unlimited.topic = "/camera/image_raw";
  out_unlimited.direction = bridge_config::Direction::Out;
  config.topics.push_back(out_unlimited);

  // Non-positive cap: excluded.
  bridge_config::TopicConfig out_zero;
  out_zero.topic = "/scan";
  out_zero.direction = bridge_config::Direction::Out;
  out_zero.max_rate_hz = 0.0;
  config.topics.push_back(out_zero);

  const auto limits = outboundRateLimits(config.topics);

  EXPECT_EQ(limits, (std::unordered_map<std::string, double>{{"/tf", 10.0}, {"/odom", 5.0}}));
}

TEST(RosUtilsTest, LatchedTopicsAreExcludedFromDataTrackPatterns) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;

  bridge_config::TopicConfig out_latched;
  out_latched.topic = "/tf_static";
  out_latched.direction = bridge_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  bridge_config::TopicConfig out_plain;
  out_plain.topic = "/tf";
  out_plain.direction = bridge_config::Direction::Out;
  config.topics.push_back(out_plain);

  bridge_config::TopicConfig in_latched;
  in_latched.topic = "/robot_description";
  in_latched.direction = bridge_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  bridge_config::TopicConfig in_plain;
  in_plain.topic = "/map";
  in_plain.direction = bridge_config::Direction::In;
  config.topics.push_back(in_plain);

  // Latched topics are handled by LatchedTopicForwarder, so they must not appear
  // in the DataTrack forwarding patterns.
  EXPECT_EQ(outgoingTopicPatterns(config.topics), (std::vector<std::string>{"/tf"}));
  EXPECT_EQ(incomingTopicPatterns(config.topics), (std::vector<std::string>{"/map"}));
}

TEST(RosUtilsTest, LatchedOutboundTopicsCollectsOutboundLatchedOnly) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;

  bridge_config::TopicConfig out_latched;
  out_latched.topic = "/tf_static";
  out_latched.direction = bridge_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  bridge_config::TopicConfig bidirectional_latched;
  bidirectional_latched.topic = "/params";
  bidirectional_latched.direction = bridge_config::Direction::Bidirectional;
  bidirectional_latched.latched = true;
  config.topics.push_back(bidirectional_latched);

  // Inbound-only latched: excluded from the outbound set.
  bridge_config::TopicConfig in_latched;
  in_latched.topic = "/robot_description";
  in_latched.direction = bridge_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  // Outbound but not latched: excluded.
  bridge_config::TopicConfig out_plain;
  out_plain.topic = "/tf";
  out_plain.direction = bridge_config::Direction::Out;
  config.topics.push_back(out_plain);

  EXPECT_EQ(latchedOutboundTopics(config.topics), (std::unordered_set<std::string>{"/tf_static", "/params"}));
}

TEST(RosUtilsTest, LatchedInboundTopicsNormalizesInboundLatchedOnly) {
  namespace bridge_config = ::ros2_livekit_bridge_config;

  bridge_config::BridgeConfig config;

  bridge_config::TopicConfig in_latched;
  in_latched.topic = "tf_static"; // no leading slash -> normalized
  in_latched.direction = bridge_config::Direction::In;
  in_latched.latched = true;
  config.topics.push_back(in_latched);

  bridge_config::TopicConfig bidirectional_latched;
  bidirectional_latched.topic = "/params";
  bidirectional_latched.direction = bridge_config::Direction::Bidirectional;
  bidirectional_latched.latched = true;
  config.topics.push_back(bidirectional_latched);

  // Outbound-only latched: excluded from the inbound set.
  bridge_config::TopicConfig out_latched;
  out_latched.topic = "/odom_static";
  out_latched.direction = bridge_config::Direction::Out;
  out_latched.latched = true;
  config.topics.push_back(out_latched);

  EXPECT_EQ(latchedInboundTopics(config.topics), (std::unordered_set<std::string>{"/tf_static", "/params"}));
}
} // namespace
} // namespace ros2_livekit_bridge::utils
