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

#include "ros2_livekit_bridge/ros2_cli/ros2_topic_list.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ros2_livekit_bridge
{
namespace
{

TopicListOptions makeOptions(
  bool show_types = false, bool count_topics = false,
  bool include_hidden_topics = false,
  bool verbose = false)
{
  TopicListOptions options;
  options.show_types = show_types;
  options.count_topics = count_topics;
  options.include_hidden_topics = include_hidden_topics;
  options.verbose = verbose;
  return options;
}

TEST(Ros2TopicListTest, NonVerboseListsTopicNamesOnePerLine) {
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 0, 0},
    {"/beta", {"std_msgs/msg/Bool"}, 0, 0},
  };

  EXPECT_EQ(ros2_cli::formatTopicList(topics, makeOptions()),
            "/alpha\n/beta\n");
}

TEST(Ros2TopicListTest, ShowTypesListsTopicTypes) {
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 0, 0},
    {"/beta", {"std_msgs/msg/Bool", "custom_msgs/msg/Thing"}, 0, 0},
  };

  EXPECT_EQ(ros2_cli::formatTopicList(topics, makeOptions(true)),
            "/alpha [std_msgs/msg/String]\n"
            "/beta [std_msgs/msg/Bool, custom_msgs/msg/Thing]\n");
}

TEST(Ros2TopicListTest, CountTopicsOnlyPrintsTopicCount) {
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 1, 2},
    {"/beta", {"std_msgs/msg/Bool"}, 3, 1},
  };

  EXPECT_EQ(
      ros2_cli::formatTopicList(topics, makeOptions(true, true, false, true)),
      "2\n");
}

TEST(Ros2TopicListTest, VerboseListsPublishedAndSubscribedTopics) {
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 1, 2},
    {"/beta", {"std_msgs/msg/String", "custom_msgs/msg/Thing"}, 3, 1},
    {"/quiet", {"std_msgs/msg/Bool"}, 0, 0},
  };

  EXPECT_EQ(
      ros2_cli::formatTopicList(topics, makeOptions(false, false, false, true)),
      "Published topics:\n"
      " * /alpha [std_msgs/msg/String] 1 publisher\n"
      " * /beta [std_msgs/msg/String, custom_msgs/msg/Thing] 3 publishers\n"
      "\n"
      "Subscribed topics:\n"
      " * /alpha [std_msgs/msg/String] 2 subscribers\n"
      " * /beta [std_msgs/msg/String, custom_msgs/msg/Thing] 1 subscriber\n");
}

TEST(Ros2TopicListTest, DetectsHiddenTopicTokens) {
  EXPECT_FALSE(ros2_cli::isHiddenTopic("/visible/topic"));
  EXPECT_TRUE(ros2_cli::isHiddenTopic("/_hidden/topic"));
  EXPECT_TRUE(ros2_cli::isHiddenTopic("/visible/_hidden"));
}

} // namespace
} // namespace ros2_livekit_bridge
