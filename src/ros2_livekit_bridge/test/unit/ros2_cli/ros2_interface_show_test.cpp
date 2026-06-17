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

#include "ros2_livekit_bridge/ros2_cli/ros2_interface_show.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace ros2_livekit_bridge
{
namespace
{

InterfaceShowOptions
makeInterfaceOptions(
  std::string type = "std_msgs/msg/Header",
  bool all_comments = false, bool no_comments = false)
{
  InterfaceShowOptions options;
  options.type = std::move(type);
  options.all_comments = all_comments;
  options.no_comments = no_comments;
  return options;
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithTopLevelComments) {
  const auto output =
    ros2_cli::renderInterfaceDefinition(makeInterfaceOptions());

  EXPECT_NE(
      output.find("# Standard metadata for higher-level stamped data types."),
      std::string::npos);
  EXPECT_NE(output.find("builtin_interfaces/Time stamp"), std::string::npos);
  EXPECT_NE(output.find("\tint32 sec"), std::string::npos);
  EXPECT_EQ(output.find("# This message communicates ROS Time defined here:"),
            std::string::npos);
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithAllComments) {
  const auto output = ros2_cli::renderInterfaceDefinition(
      makeInterfaceOptions("std_msgs/msg/Header", true));

  EXPECT_NE(output.find("# This message communicates ROS Time defined here:"),
            std::string::npos);
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithoutCommentsOrWhitespace) {
  const auto output = ros2_cli::renderInterfaceDefinition(
      makeInterfaceOptions("std_msgs/msg/Header", false, true));

  EXPECT_EQ(output, "builtin_interfaces/Time stamp\n"
                    "\tint32 sec\n"
                    "\tuint32 nanosec\n"
                    "string frame_id\n");
}

TEST(Ros2InterfaceShowTest, InterfaceShowRejectsInvalidArguments) {
  EXPECT_THROW(ros2_cli::renderInterfaceDefinition(makeInterfaceOptions("")),
               std::runtime_error);
  EXPECT_THROW(ros2_cli::renderInterfaceDefinition(makeInterfaceOptions("-")),
               std::runtime_error);
  EXPECT_THROW(ros2_cli::renderInterfaceDefinition(
                   makeInterfaceOptions("std_msgs/msg/Header", true, true)),
               std::runtime_error);
}

} // namespace
} // namespace ros2_livekit_bridge
