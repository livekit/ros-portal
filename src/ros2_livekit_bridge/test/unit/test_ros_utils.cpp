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

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace livekit::ros_bridge::utils
{
namespace
{

constexpr const char *kTestEnvVar = "ROS2_LIVEKIT_BRIDGE_TEST_CREDENTIAL";

struct ScopedEnvVar
{
  explicit ScopedEnvVar(const char *name)
  : name(name)
  {
    const char *value = std::getenv(name);
    if (value) {
      had_value = true;
      original_value = value;
    }
  }

  ~ScopedEnvVar()
  {
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

  const auto patterns = outgoingTopicPatterns(config);

  EXPECT_EQ(
    patterns,
    (std::vector<std::string>{"/camera/image_raw", "/odom"}));
}

} // namespace
} // namespace livekit::ros_bridge::utils
