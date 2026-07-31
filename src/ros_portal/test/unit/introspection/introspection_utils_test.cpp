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

#include "ros_portal/introspection/introspection_utils.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>
#include <string>
#include <test_msgs/msg/basic_types.hpp>
#include <test_msgs/msg/builtins.hpp>
#include <test_msgs/msg/nested.hpp>
#include <test_msgs/msg/strings.hpp>

#include "ros_portal/cli/constants.hpp"
#include "ros_portal/introspection/runtime_type_support.hpp"

namespace ros_portal::introspection {
namespace {

using ros_portal::cli::kMaxResizableSequenceLength;
using ros_portal::cli::kMaxYamlPayloadBytes;

std::string buildIntegerSequencePayload(const std::size_t count) {
  std::string payload = "[";
  payload.reserve(count * 2U + 2U);
  for (std::size_t index = 0; index < count; ++index) {
    if (index > 0) {
      payload += ",";
    }
    payload += "0";
  }
  payload += "]";
  return payload;
}

/// @brief Load runtime type support for a message type identifier.
///
/// Reuses the production loader so the introspection library is kept alive for
/// the returned value's scope and @ref RuntimeMessageTypeSupport::members points
/// into it.
RuntimeMessageTypeSupport loadIntrospection(const std::string& type) { return RuntimeMessageTypeSupport(type); }

TEST(MessageRenderTest, RendersScalarFieldsAsYaml) {
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

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  EXPECT_TRUE(output["bool_value"].as<bool>());
  EXPECT_EQ(output["byte_value"].as<unsigned>(), 7U);
  EXPECT_EQ(output["char_value"].as<unsigned>(), 65U);
  EXPECT_FLOAT_EQ(output["float32_value"].as<float>(), 1.5F);
  EXPECT_DOUBLE_EQ(output["float64_value"].as<double>(), 2.5);
  EXPECT_EQ(output["int8_value"].as<int>(), -8);
  EXPECT_EQ(output["uint8_value"].as<unsigned>(), 200U);
  EXPECT_EQ(output["int16_value"].as<int>(), -1600);
  EXPECT_EQ(output["uint16_value"].as<unsigned>(), 1600U);
  EXPECT_EQ(output["int32_value"].as<int>(), -42);
  EXPECT_EQ(output["uint32_value"].as<unsigned>(), 42U);
  EXPECT_EQ(output["int64_value"].as<long long>(), -1234567890LL);
  EXPECT_EQ(output["uint64_value"].as<unsigned long long>(), 1234567890ULL);
}

TEST(MessageRenderTest, RendersStringsThatNeedYamlEscaping) {
  const auto loaded = loadIntrospection("test_msgs/msg/Strings");
  test_msgs::msg::Strings message;
  message.string_value = "reason: timeout\nretry \"soon\"";

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  EXPECT_EQ(output["string_value"].as<std::string>(), "reason: timeout\nretry \"soon\"");
}

TEST(MessageRenderTest, RendersPlainStrings) {
  const auto loaded = loadIntrospection("test_msgs/msg/Strings");
  test_msgs::msg::Strings message;
  message.string_value = "plain-text";

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  EXPECT_EQ(output["string_value"].as<std::string>(), "plain-text");
}

TEST(MessageRenderTest, RendersNestedMessage) {
  const auto loaded = loadIntrospection("test_msgs/msg/Nested");
  test_msgs::msg::Nested message;
  message.basic_types_value.int32_value = 99;

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  EXPECT_EQ(output["basic_types_value"]["int32_value"].as<int>(), 99);
}

TEST(MessageRenderTest, RendersMultipleNestedMessageFields) {
  const auto loaded = loadIntrospection("test_msgs/msg/Builtins");
  test_msgs::msg::Builtins message;
  message.duration_value.sec = 1;
  message.duration_value.nanosec = 2;
  message.time_value.sec = 3;
  message.time_value.nanosec = 4;

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  EXPECT_EQ(output["duration_value"]["sec"].as<int>(), 1);
  EXPECT_EQ(output["duration_value"]["nanosec"].as<unsigned>(), 2U);
  EXPECT_EQ(output["time_value"]["sec"].as<int>(), 3);
  EXPECT_EQ(output["time_value"]["nanosec"].as<unsigned>(), 4U);
}

TEST(MessageRenderTest, RendersSequenceField) {
  const auto loaded = loadIntrospection("std_msgs/msg/Int32MultiArray");
  std_msgs::msg::Int32MultiArray message;
  message.data = {1, 2, 3};

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  ASSERT_TRUE(output["data"].IsSequence());
  ASSERT_EQ(output["data"].size(), 3U);
  EXPECT_EQ(output["data"][0].as<int>(), 1);
  EXPECT_EQ(output["data"][1].as<int>(), 2);
  EXPECT_EQ(output["data"][2].as<int>(), 3);
  // An empty sequence is rendered as the YAML null scalar '~' rather than an
  // empty flow sequence '[]', so layout.dim comes through as null here.
  EXPECT_TRUE(output["layout"]["dim"].IsNull());
}

TEST(MessageRenderTest, RendersUint8SequenceField) {
  const auto loaded = loadIntrospection("std_msgs/msg/UInt8MultiArray");
  std_msgs::msg::UInt8MultiArray message;
  message.data.resize(3U);
  message.data[0] = 1U;
  message.data[1] = 2U;
  message.data[2] = 255U;

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  ASSERT_TRUE(output["data"].IsSequence());
  ASSERT_EQ(output["data"].size(), 3U);
  EXPECT_EQ(output["data"][0].as<unsigned>(), 1U);
  EXPECT_EQ(output["data"][1].as<unsigned>(), 2U);
  EXPECT_EQ(output["data"][2].as<unsigned>(), 255U);
}

TEST(MessageRenderTest, RendersNestedMessageSequence) {
  const auto loaded = loadIntrospection("std_msgs/msg/Int32MultiArray");
  std_msgs::msg::Int32MultiArray message;
  message.layout.dim.resize(1U);
  message.layout.dim[0].label = "width";
  message.layout.dim[0].size = 2U;
  message.layout.dim[0].stride = 3U;
  message.data = {1, 2, 3};

  const YAML::Node output = YAML::Load(toYaml(loaded.members, &message));

  ASSERT_TRUE(output["layout"]["dim"].IsSequence());
  ASSERT_EQ(output["layout"]["dim"].size(), 1U);
  EXPECT_EQ(output["layout"]["dim"][0]["label"].as<std::string>(), "width");
  EXPECT_EQ(output["layout"]["dim"][0]["size"].as<unsigned>(), 2U);
  EXPECT_EQ(output["layout"]["dim"][0]["stride"].as<unsigned>(), 3U);
}

TEST(ContainsOversizedSequenceTest, IgnoresScalarsAndSmallSequences) {
  EXPECT_FALSE(containsOversizedSequence(YAML::Node{}));
  EXPECT_FALSE(containsOversizedSequence(YAML::Load("42")));
  EXPECT_FALSE(containsOversizedSequence(YAML::Load("[1, 2, 3]")));
}

TEST(ContainsOversizedSequenceTest, DetectsTopLevelOversizedSequence) {
  const auto payload = buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U);
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, AcceptsSequenceAtMaximumLength) {
  const auto payload = buildIntegerSequencePayload(kMaxResizableSequenceLength);
  EXPECT_FALSE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, DetectsNestedOversizedSequenceInMap) {
  const auto payload = "{data: " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "}";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, DetectsNestedOversizedSequenceInSequence) {
  const auto payload = "[[] , " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "]";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, DetectsDeeplyNestedOversizedSequence) {
  // Oversized sequence buried several map/sequence levels deep: map -> seq -> map -> seq.
  const auto payload =
      "{outer: [{middle: {inner: " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "}}]}";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, RecursesPastEarlierMapKeys) {
  // The oversized sequence lives under a later map key; recursion must not stop at the first.
  const auto payload =
      "{first: 1, second: [1, 2], third: " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "}";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, RecursesPastEarlierSequenceElements) {
  // The oversized sequence lives under a later sequence element after small scalars/maps.
  const auto payload = "[0, {a: 1}, " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "]";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, AcceptsDeeplyNestedSmallSequences) {
  // Every nested sequence is within bounds, even though the structure is deep.
  const auto inner = buildIntegerSequencePayload(kMaxResizableSequenceLength);
  const auto payload = "{outer: [{middle: {inner: " + inner + "}}, " + inner + "]}";
  EXPECT_FALSE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, AcceptsEmptyContainers) {
  EXPECT_FALSE(containsOversizedSequence(YAML::Load("[]")));
  EXPECT_FALSE(containsOversizedSequence(YAML::Load("{}")));
  EXPECT_FALSE(containsOversizedSequence(YAML::Load("{a: [], b: {}}")));
}

TEST(ContainsOversizedSequenceTest, IgnoresOversizedMap) {
  // A map with many keys is not a sequence and must not be flagged.
  std::string payload = "{";
  for (std::size_t index = 0; index <= kMaxResizableSequenceLength; ++index) {
    if (index > 0) {
      payload += ",";
    }
    payload += "k" + std::to_string(index) + ": 0";
  }
  payload += "}";
  EXPECT_FALSE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(ContainsOversizedSequenceTest, DetectsSecondOfSiblingSequences) {
  // First sibling sequence is in-bounds; the second is oversized.
  const auto payload = "{a: " + buildIntegerSequencePayload(kMaxResizableSequenceLength) +
                       ", b: " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "}";
  EXPECT_TRUE(containsOversizedSequence(YAML::Load(payload)));
}

TEST(LoadPayloadTest, RejectsEmptyPayload) {
  std::string error;
  const auto root = loadPayload("", error);
  EXPECT_FALSE(root.has_value());
  EXPECT_EQ(error, "payload must be non-empty");
}

TEST(LoadPayloadTest, RejectsOversizedPayload) {
  const std::string payload(kMaxYamlPayloadBytes + 1U, 'a');
  std::string error;
  const auto root = loadPayload(payload, error);
  EXPECT_FALSE(root.has_value());
  EXPECT_EQ(error, "payload exceeds maximum size of " + std::to_string(kMaxYamlPayloadBytes) + " bytes");
}

TEST(LoadPayloadTest, RejectsMalformedYaml) {
  std::string error;
  const auto root = loadPayload("{data: [", error);
  EXPECT_FALSE(root.has_value());
  EXPECT_NE(error.find("payload is not valid YAML:"), std::string::npos);
}

TEST(LoadPayloadTest, RejectsOversizedSequence) {
  const auto payload = "{data: " + buildIntegerSequencePayload(kMaxResizableSequenceLength + 1U) + "}";
  std::string error;
  const auto root = loadPayload(payload, error);
  EXPECT_FALSE(root.has_value());
  EXPECT_EQ(error,
            "payload contains a sequence exceeding maximum length " + std::to_string(kMaxResizableSequenceLength));
}

TEST(LoadPayloadTest, ParsesValidPayload) {
  std::string error;
  const auto root = loadPayload("{data: hello}", error);
  ASSERT_TRUE(root.has_value());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ((*root)["data"].as<std::string>(), "hello");
}

} // namespace
} // namespace ros_portal::introspection
