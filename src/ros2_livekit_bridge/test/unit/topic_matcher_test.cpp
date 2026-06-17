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

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils
{
namespace
{

TEST(TopicMatcherTest, MatchesAnyCompiledPattern) {
  const auto patterns =
    compileRegexPatterns(std::vector<std::string>{"/camera/.*", "/tf"});

  EXPECT_TRUE(matchesAnyPattern("/camera/image_raw", patterns));
  EXPECT_TRUE(matchesAnyPattern("/tf", patterns));
}

TEST(TopicMatcherTest, RejectsValuesThatDoNotFullyMatch) {
  const auto patterns = compileRegexPatterns(std::vector<std::string>{"/tf"});

  EXPECT_FALSE(matchesAnyPattern("/tf_static", patterns));
}

TEST(TopicMatcherTest, EmptyPatternListDoesNotMatch) {
  EXPECT_FALSE(matchesAnyPattern("/camera/image_raw", {}));
}

TEST(TopicMatcherTest, ReportsInvalidPatternsAndKeepsValidPatterns) {
  std::vector<PatternCompileError> errors;
  const auto patterns =
    compileRegexPatterns(std::vector<std::string>{"/tf", "["}, &errors);

  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.front().pattern, "[");
  EXPECT_FALSE(errors.front().message.empty());
  EXPECT_TRUE(matchesAnyPattern("/tf", patterns));
}

TEST(TopicMatcherTest, MatchesTopicRoutes) {
  TopicRouteTable route_table;
  route_table.outgoing.push_back(
    CompiledTopicRoute{"/camera/.*", std::regex("/camera/.*"), {}});
  route_table.outgoing.push_back(
    CompiledTopicRoute{"/tf", std::regex("/tf"), {}});

  EXPECT_TRUE(matchesTopicRoutes("/camera/image_raw", route_table.outgoing));
  EXPECT_TRUE(matchesTopicRoutes("/tf", route_table.outgoing));
  EXPECT_FALSE(matchesTopicRoutes("/tf_static", route_table.outgoing));
}

TEST(TopicMatcherTest, MatchesTopicRoutesForAnyParticipantWhenUnscoped) {
  TopicRouteTable route_table;
  route_table.incoming.push_back(
    CompiledTopicRoute{"/cmd_vel", std::regex("/cmd_vel"), {}});
  route_table.incoming_by_participant[""].push_back(0);

  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_b", route_table.incoming,
    route_table.incoming_by_participant));
}

TEST(TopicMatcherTest, MatchesTopicRoutesForSpecificParticipant) {
  TopicRouteTable route_table;
  route_table.incoming.push_back(
    CompiledTopicRoute{"/cmd_vel", std::regex("/cmd_vel"), {"robot_a"}});
  route_table.incoming.push_back(
    CompiledTopicRoute{"/odom", std::regex("/odom"), {"robot_b"}});
  route_table.incoming_by_participant["robot_a"].push_back(0);
  route_table.incoming_by_participant["robot_b"].push_back(1);

  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_FALSE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_b", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/odom", "robot_b", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_FALSE(matchesTopicRoutesForParticipant(
    "/odom", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
}

TEST(TopicMatcherTest, FallsBackToAnyParticipantRoutesWhenParticipantSpecificMisses) {
  TopicRouteTable route_table;
  route_table.incoming.push_back(
    CompiledTopicRoute{"/cmd_vel", std::regex("/cmd_vel"), {"robot_a"}});
  route_table.incoming.push_back(
    CompiledTopicRoute{"/.*", std::regex("/.*"), {}});
  route_table.incoming_by_participant["robot_a"].push_back(0);
  route_table.incoming_by_participant[""].push_back(1);

  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/other", "robot_b", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/other", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
}

TEST(TopicMatcherTest, AppendTopicRouteIndexesParticipantScopedRoutes) {
  TopicRouteTable route_table;
  appendTopicRoute(
    route_table, "/cmd_vel", {"robot_a", "robot_b"}, false, true);

  ASSERT_EQ(route_table.incoming.size(), 1u);
  EXPECT_EQ(route_table.incoming.front().participants.size(), 2u);
  EXPECT_EQ(route_table.incoming_by_participant.at("robot_a").size(), 1u);
  EXPECT_EQ(route_table.incoming_by_participant.at("robot_b").size(), 1u);
  EXPECT_FALSE(route_table.incoming_by_participant.count("") > 0);
}

TEST(TopicMatcherTest, AppendTopicRouteIndexesWildcardParticipantRoutes) {
  TopicRouteTable route_table;
  appendTopicRoute(route_table, "/odom", {}, true, true);

  ASSERT_EQ(route_table.outgoing.size(), 1u);
  ASSERT_EQ(route_table.incoming.size(), 1u);
  EXPECT_EQ(route_table.outgoing_by_participant.at("").size(), 1u);
  EXPECT_EQ(route_table.incoming_by_participant.at("").size(), 1u);
}

TEST(TopicMatcherTest, AppendTopicRouteCollectsInvalidPatterns) {
  TopicRouteTable route_table;
  appendTopicRoute(route_table, "[", {}, true, false);

  ASSERT_EQ(route_table.errors.size(), 1u);
  EXPECT_EQ(route_table.errors.front().pattern, "[");
  EXPECT_TRUE(route_table.outgoing.empty());
}

TEST(TopicMatcherTest, RejectsUnknownParticipantWhenOnlyScopedRoutesExist) {
  TopicRouteTable route_table;
  appendTopicRoute(route_table, "/cmd_vel", {"robot_a"}, false, true);

  EXPECT_TRUE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_a", route_table.incoming,
    route_table.incoming_by_participant));
  EXPECT_FALSE(matchesTopicRoutesForParticipant(
    "/cmd_vel", "robot_b", route_table.incoming,
    route_table.incoming_by_participant));
}

} // namespace
} // namespace ros2_livekit_bridge::utils
