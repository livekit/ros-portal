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

#include "ros2_livekit_bridge/introspection/message_render.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <rclcpp/typesupport_helpers.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <test_msgs/msg/basic_types.hpp>
#include <test_msgs/msg/nested.hpp>

namespace ros2_livekit_bridge::message_render
{
namespace
{

/// @brief Loaded introspection type support kept alive for the test scope.
///
/// The MessageMembers metadata points into @ref library, so the library must
/// outlive any rendering call that consumes @ref members.
struct LoadedIntrospection
{
  /// @brief Library owning the introspection type-support handle.
  std::shared_ptr<rcpputils::SharedLibrary> library;
  /// @brief Message member metadata for the loaded type.
  const introspection::MessageMembers * members{nullptr};
};

/// @brief Load introspection members for a message type identifier.
LoadedIntrospection loadIntrospection(const std::string & type)
{
  LoadedIntrospection loaded;
  loaded.library = rclcpp::get_typesupport_library(
    type, introspection::typesupport_identifier);
  const auto * handle = rclcpp::get_message_typesupport_handle(
    type, introspection::typesupport_identifier, *loaded.library);
  loaded.members =
    static_cast<const introspection::MessageMembers *>(handle->data);
  return loaded;
}

/// @brief Find a member by name within introspection metadata.
const introspection::MessageMember * memberByName(
  const introspection::MessageMembers & members,
  const std::string & name)
{
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    if (name == members.members_[index].name_) {
      return &members.members_[index];
    }
  }
  throw std::runtime_error("member not found: " + name);
}

TEST(MessageRenderTest, MemberMemoryPointsAtField)
{
  const auto loaded = loadIntrospection("test_msgs/msg/BasicTypes");
  test_msgs::msg::BasicTypes message;
  message.int32_value = 123;

  const auto * member = memberByName(*loaded.members, "int32_value");
  const void * field = memberMemory(&message, *member);

  EXPECT_EQ(field, static_cast<const void *>(&message.int32_value));
  EXPECT_EQ(*static_cast<const std::int32_t *>(field), 123);
}

TEST(MessageRenderTest, RendersScalarFieldsAsYaml)
{
  const auto loaded = loadIntrospection("test_msgs/msg/BasicTypes");
  test_msgs::msg::BasicTypes message;
  message.bool_value = true;
  message.byte_value = 7;
  message.char_value = 65;
  message.float32_value = 1.5f;
  message.float64_value = 2.5;
  message.int8_value = -8;
  message.uint8_value = 200;
  message.int16_value = -1600;
  message.uint16_value = 1600;
  message.int32_value = -42;
  message.uint32_value = 42;
  message.int64_value = -1234567890;
  message.uint64_value = 1234567890;

  const std::string output = toYaml(*loaded.members, &message);

  EXPECT_NE(output.find("bool_value: true"), std::string::npos) << output;
  EXPECT_NE(output.find("byte_value: 7"), std::string::npos) << output;
  EXPECT_NE(output.find("char_value: 65"), std::string::npos) << output;
  EXPECT_NE(output.find("float32_value: 1.5"), std::string::npos) << output;
  EXPECT_NE(output.find("float64_value: 2.5"), std::string::npos) << output;
  EXPECT_NE(output.find("int8_value: -8"), std::string::npos) << output;
  EXPECT_NE(output.find("uint8_value: 200"), std::string::npos) << output;
  EXPECT_NE(output.find("int16_value: -1600"), std::string::npos) << output;
  EXPECT_NE(output.find("uint16_value: 1600"), std::string::npos) << output;
  EXPECT_NE(output.find("int32_value: -42"), std::string::npos) << output;
  EXPECT_NE(output.find("uint32_value: 42"), std::string::npos) << output;
  EXPECT_NE(output.find("int64_value: -1234567890"), std::string::npos)
    << output;
  EXPECT_NE(output.find("uint64_value: 1234567890"), std::string::npos)
    << output;
}

TEST(MessageRenderTest, RendersNestedMessageWithAccumulatingIndent)
{
  const auto loaded = loadIntrospection("test_msgs/msg/Nested");
  test_msgs::msg::Nested message;
  message.basic_types_value.int32_value = 99;

  const std::string output = toYaml(*loaded.members, &message);

  // The nested message opens on its own line and its fields are indented two
  // spaces relative to the parent field name.
  EXPECT_NE(output.find("basic_types_value: \n"), std::string::npos) << output;
  EXPECT_NE(output.find("\n  int32_value: 99\n"), std::string::npos) << output;
}

TEST(MessageRenderTest, RendersSequenceFieldAsCompactList)
{
  const auto loaded = loadIntrospection("std_msgs/msg/Int32MultiArray");
  std_msgs::msg::Int32MultiArray message;
  message.data = {1, 2, 3};

  const std::string output = toYaml(*loaded.members, &message);

  EXPECT_NE(output.find("data: [1, 2, 3]"), std::string::npos) << output;
  // An empty nested sequence renders as an empty list rather than being omitted.
  EXPECT_NE(output.find("dim: []"), std::string::npos) << output;
}

}  // namespace
}  // namespace ros2_livekit_bridge::message_render
