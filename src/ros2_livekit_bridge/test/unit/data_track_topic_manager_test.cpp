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

#include "ros2_livekit_bridge/managers/data_track_topic_manager.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <gtest/gtest.h>

namespace ros2_livekit_bridge::managers
{
namespace
{

class DataTrackTopicManagerTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    route_table_.incoming.push_back(
      utils::CompiledTopicRoute{"/cmd_vel", std::regex("/cmd_vel"), {"robot_a"}});
    route_table_.incoming.push_back(
      utils::CompiledTopicRoute{"/odom", std::regex("/odom"), {}});
    route_table_.incoming_by_participant["robot_a"].push_back(0);
    route_table_.incoming_by_participant[""].push_back(1);

    route_table_.outgoing.push_back(
      utils::CompiledTopicRoute{"/bridge/out", std::regex("/bridge/out"), {}});
    route_table_.outgoing_by_participant[""].push_back(0);

    DataTrackTopicManager::Dependencies dependencies;
    dependencies.topic_routes = &route_table_;
    manager_ = std::make_unique<DataTrackTopicManager>(std::move(dependencies));
  }

  utils::TopicRouteTable route_table_;
  std::unique_ptr<DataTrackTopicManager> manager_;
};

TEST_F(DataTrackTopicManagerTest, MatchesInboundRouteForParticipantScope) {
  EXPECT_TRUE(manager_->matchesInboundRoute("/cmd_vel", "robot_a"));
  EXPECT_FALSE(manager_->matchesInboundRoute("/cmd_vel", "robot_b"));
  EXPECT_TRUE(manager_->matchesInboundRoute("/odom", "robot_b"));
}

TEST_F(DataTrackTopicManagerTest, MatchesOutboundRoute) {
  EXPECT_TRUE(manager_->matchesOutboundRoute("/bridge/out"));
  EXPECT_FALSE(manager_->matchesOutboundRoute("/other"));
}

TEST_F(DataTrackTopicManagerTest, ShutdownWithoutSubscriptionsIsSafe) {
  EXPECT_NO_THROW(manager_->shutdown());
  EXPECT_FALSE(manager_->hasOutboundSubscription("/bridge/out"));
  EXPECT_FALSE(manager_->isInboundManagedRosTopic("/bridge/out"));
}

TEST_F(DataTrackTopicManagerTest, RejectsInboundTrackWithoutMatchingRoute) {
  livekit::DataTrackPublishedEvent event;
  EXPECT_NO_THROW(manager_->onDataTrackPublished(event));
}

} // namespace
} // namespace ros2_livekit_bridge::managers
