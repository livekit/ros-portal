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

#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge::ros2_cli
{
namespace
{

using json = nlohmann::json;

TEST(Ros2CliUtilsTest, DetectsHiddenNameTokens) {
  EXPECT_FALSE(hasHiddenNameToken(""));
  EXPECT_FALSE(hasHiddenNameToken("/visible/name"));
  EXPECT_FALSE(hasHiddenNameToken("visible/name"));
  EXPECT_FALSE(hasHiddenNameToken("visible_underscore/name"));

  EXPECT_TRUE(hasHiddenNameToken("_hidden"));
  EXPECT_TRUE(hasHiddenNameToken("/_hidden/name"));
  EXPECT_TRUE(hasHiddenNameToken("/visible/_hidden"));
  EXPECT_TRUE(hasHiddenNameToken("///visible///_hidden"));
}

TEST(Ros2CliUtilsTest, JoinsTypesWithCommaSeparators) {
  EXPECT_EQ(joinTypes({}), "");
  EXPECT_EQ(joinTypes({"std_msgs/msg/String"}), "std_msgs/msg/String");
  EXPECT_EQ(joinTypes({"std_msgs/msg/String", "std_msgs/msg/Header"}),
            "std_msgs/msg/String, std_msgs/msg/Header");
}

TEST(Ros2CliUtilsTest, TrimsLeadingAndTrailingWhitespace) {
  EXPECT_EQ(leftTrim("  hello"), "hello");
  EXPECT_EQ(leftTrim("\t\nvalue"), "value");
  EXPECT_EQ(leftTrim("no_trim"), "no_trim");
  EXPECT_EQ(leftTrim(""), "");
  EXPECT_EQ(leftTrim("   "), "");

  EXPECT_EQ(rightTrim("hello  "), "hello");
  EXPECT_EQ(rightTrim("value\t\n"), "value");
  EXPECT_EQ(rightTrim("no_trim"), "no_trim");
  EXPECT_EQ(rightTrim(""), "");
  EXPECT_EQ(rightTrim("   "), "");
}

TEST(Ros2CliUtilsTest, ReadsRequiredStringField) {
  const json body{{"topic", " /cmd_vel "}};

  EXPECT_EQ(
    requiredStringField(
      body, "topic", "topic must be a string", "topic must be non-empty"),
    "/cmd_vel");
}

TEST(Ros2CliUtilsTest, RejectsMissingRequiredStringField) {
  const json body = json::object();

  EXPECT_THROW(
    requiredStringField(
      body, "topic", "topic must be a string", "topic must be non-empty"),
    std::invalid_argument);
}

TEST(Ros2CliUtilsTest, RejectsNonStringRequiredStringField) {
  const json body{{"topic", 42}};

  EXPECT_THROW(
    requiredStringField(
      body, "topic", "topic must be a string", "topic must be non-empty"),
    std::invalid_argument);
}

TEST(Ros2CliUtilsTest, RejectsEmptyRequiredStringField) {
  const json body{{"topic", " \t\n "}};

  EXPECT_THROW(
    requiredStringField(
      body, "topic", "topic must be a string", "topic must be non-empty"),
    std::invalid_argument);
}

TEST(Ros2CliUtilsTest, MatchesTopicTypeFromGraphTypes) {
  EXPECT_TRUE(
    topicTypeMatches(
      {"std_msgs/msg/String", "std_msgs/msg/Header"},
      "std_msgs/msg/String"));
  EXPECT_TRUE(
    topicTypeMatches(
      {"std_msgs/msg/String", "std_msgs/msg/Header"},
      "std_msgs/msg/Header"));
}

TEST(Ros2CliUtilsTest, RejectsMissingTopicTypeFromGraphTypes) {
  EXPECT_FALSE(
    topicTypeMatches(
      {"std_msgs/msg/String", "std_msgs/msg/Header"},
      "geometry_msgs/msg/Twist"));
  EXPECT_FALSE(topicTypeMatches({}, "std_msgs/msg/String"));
}

} // namespace
} // namespace ros2_livekit_bridge::ros2_cli
