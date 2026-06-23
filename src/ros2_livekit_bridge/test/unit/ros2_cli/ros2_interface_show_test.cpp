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

#include <sstream>
#include <string>
#include <utility>

namespace ros2_livekit_bridge
{
namespace
{

using ros2_cli::interface_show::utils::appendRenderedInterfaceLine;
using ros2_cli::interface_show::utils::interfacePath;
using ros2_cli::interface_show::utils::isPrimitiveInterfaceType;
using ros2_cli::interface_show::utils::nestedInterfaceTypeFromLine;
using ros2_cli::interface_show::utils::removeArraySuffix;
using ros2_cli::interface_show::utils::splitInterfaceType;
using ros2_cli::interface_show::utils::stripTrailingComment;

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

TEST(Ros2InterfaceShowUtilsTest, SplitsInterfaceTypeOnSlashes) {
  EXPECT_EQ(splitInterfaceType("std_msgs/msg/String"),
    (std::vector<std::string>{"std_msgs", "msg", "String"}));
  EXPECT_EQ(splitInterfaceType("pkg/"),
    (std::vector<std::string>{"pkg", ""}));
}

TEST(Ros2InterfaceShowUtilsTest, RemovesArrayAndBoundedSuffixes) {
  EXPECT_EQ(removeArraySuffix("int32[10]"), "int32");
  EXPECT_EQ(removeArraySuffix("string<=10"), "string");
  EXPECT_EQ(removeArraySuffix("float64"), "float64");
}

TEST(Ros2InterfaceShowUtilsTest, DetectsPrimitiveInterfaceTypes) {
  EXPECT_TRUE(isPrimitiveInterfaceType("int32"));
  EXPECT_TRUE(isPrimitiveInterfaceType("string"));
  EXPECT_FALSE(isPrimitiveInterfaceType("std_msgs/String"));
  EXPECT_FALSE(isPrimitiveInterfaceType("Header"));
}

TEST(Ros2InterfaceShowUtilsTest, StripsTrailingComments) {
  EXPECT_EQ(stripTrailingComment("int32 value  # comment"), "int32 value");
  EXPECT_EQ(stripTrailingComment("  # full-line comment  "), "");
  EXPECT_EQ(stripTrailingComment("no_comment"), "no_comment");
}

TEST(Ros2InterfaceShowUtilsTest, InfersNestedInterfaceTypesFromLines) {
  EXPECT_EQ(
    nestedInterfaceTypeFromLine("std_msgs", "builtin_interfaces/Time stamp"),
    "builtin_interfaces/msg/Time");
  EXPECT_EQ(
    nestedInterfaceTypeFromLine("std_msgs", "Header header"),
    "std_msgs/msg/Header");
  EXPECT_EQ(
    nestedInterfaceTypeFromLine("std_msgs", "int32 value"),
    std::nullopt);
  EXPECT_EQ(
    nestedInterfaceTypeFromLine("std_msgs", "string frame_id"),
    std::nullopt);
  EXPECT_EQ(
    nestedInterfaceTypeFromLine("std_msgs", "---"),
    std::nullopt);
}

TEST(Ros2InterfaceShowUtilsTest, ResolvesInstalledInterfacePaths) {
  const auto path = interfacePath("std_msgs/msg/Header");
  ASSERT_TRUE(path.has_value());
  EXPECT_NE(path->find("std_msgs/msg/Header.msg"), std::string::npos);
  EXPECT_FALSE(interfacePath("invalid").has_value());
  EXPECT_FALSE(interfacePath("pkg/unknown/Name").has_value());
}

TEST(Ros2InterfaceShowUtilsTest, AppendsRenderedInterfaceLines) {
  std::ostringstream with_comments;
  appendRenderedInterfaceLine(with_comments, "  # comment", true, 1);
  EXPECT_EQ(with_comments.str(), "\t  # comment\n");

  std::ostringstream without_comments;
  appendRenderedInterfaceLine(without_comments, "int32 value  # inline", false, 0);
  EXPECT_EQ(without_comments.str(), "int32 value\n");

  std::ostringstream skips_comments;
  appendRenderedInterfaceLine(skips_comments, "# only comment", false, 0);
  EXPECT_TRUE(skips_comments.str().empty());
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithTopLevelComments) {
  const auto output =
    ros2_cli::renderInterfaceDefinition(makeInterfaceOptions());
  ASSERT_TRUE(output.has_value());

  EXPECT_NE(
      output->find("# Standard metadata for higher-level stamped data types."),
      std::string::npos);
  EXPECT_NE(output->find("builtin_interfaces/Time stamp"), std::string::npos);
  EXPECT_NE(output->find("\tint32 sec"), std::string::npos);
  EXPECT_EQ(output->find("# This message communicates ROS Time defined here:"),
            std::string::npos);
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithAllComments) {
  const auto output = ros2_cli::renderInterfaceDefinition(
      makeInterfaceOptions("std_msgs/msg/Header", true));
  ASSERT_TRUE(output.has_value());

  EXPECT_NE(output->find("# This message communicates ROS Time defined here:"),
            std::string::npos);
}

TEST(Ros2InterfaceShowTest, RendersInterfaceWithoutCommentsOrWhitespace) {
  const auto output = ros2_cli::renderInterfaceDefinition(
      makeInterfaceOptions("std_msgs/msg/Header", false, true));
  ASSERT_TRUE(output.has_value());

  EXPECT_EQ(*output, "builtin_interfaces/Time stamp\n"
                     "\tint32 sec\n"
                     "\tuint32 nanosec\n"
                     "string frame_id\n");
}

TEST(Ros2InterfaceShowTest, InterfaceShowRejectsInvalidArguments) {
  EXPECT_FALSE(ros2_cli::renderInterfaceDefinition(makeInterfaceOptions(""))
    .has_value());
  EXPECT_FALSE(ros2_cli::renderInterfaceDefinition(makeInterfaceOptions("-"))
    .has_value());
  EXPECT_FALSE(ros2_cli::renderInterfaceDefinition(
                   makeInterfaceOptions("std_msgs/msg/Header", true, true))
    .has_value());
}

} // namespace
} // namespace ros2_livekit_bridge
