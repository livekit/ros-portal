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

#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/path.hpp>
#include <optional>
#include <rclcpp/serialization.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/byte.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/int64.hpp>
#include <std_msgs/msg/int8.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/u_int64.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>
#include <string>
#include <test_msgs/msg/bounded_sequences.hpp>
#include <test_msgs/msg/strings.hpp>
#include <test_msgs/msg/w_strings.hpp>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"

namespace ros2_livekit_bridge {
namespace {

template <typename MessageT>
MessageT deserialize(const rclcpp::SerializedMessage& serialized) {
  rclcpp::Serialization<MessageT> serialization;
  MessageT message;
  serialization.deserialize_message(&serialized, &message);
  return message;
}

// Asserts conversion succeeds and returns the serialized message; surfaces the
// error string on failure so the test name plus message pinpoints the cause.
rclcpp::SerializedMessage serialize(const std::string& msg_type, const std::string& payload) {
  std::string error;
  auto serialized = ros2_cli::serializedMessageFromYaml(msg_type, payload, error);
  EXPECT_TRUE(serialized.has_value()) << error;
  return serialized.value_or(rclcpp::SerializedMessage{});
}

// Asserts conversion fails and reports a non-empty diagnostic message.
void expectFailure(const std::string& msg_type, const std::string& payload) {
  std::string error;
  const auto serialized = ros2_cli::serializedMessageFromYaml(msg_type, payload, error);
  EXPECT_FALSE(serialized.has_value());
  EXPECT_FALSE(error.empty());
}

// ---------------------------------------------------------------------------
// End-to-end coverage of serializedMessageFromYaml: each test drives a distinct
// branch of the message-assembly machinery (scalars, nested messages, fixed and
// resizable arrays, arrays of messages) and the error paths around them.
// ---------------------------------------------------------------------------

TEST(YamlMessageTest, SerializesStringMessage) {
  const auto serialized = serialize("std_msgs/msg/String", "{data: hello}");

  const auto message = deserialize<std_msgs::msg::String>(serialized);
  EXPECT_EQ(message.data, "hello");
}

TEST(YamlMessageTest, SerializesTwistMessage) {
  const auto serialized =
      serialize("geometry_msgs/msg/Twist", "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 1.25}}");

  const auto message = deserialize<geometry_msgs::msg::Twist>(serialized);
  EXPECT_DOUBLE_EQ(message.linear.x, 0.5);
  EXPECT_DOUBLE_EQ(message.linear.y, 0.0);
  EXPECT_DOUBLE_EQ(message.linear.z, 0.0);
  EXPECT_DOUBLE_EQ(message.angular.x, 0.0);
  EXPECT_DOUBLE_EQ(message.angular.y, 0.0);
  EXPECT_DOUBLE_EQ(message.angular.z, 1.25);
}

TEST(YamlMessageTest, SerializesPrimitiveFields) {
  const auto bool_message = deserialize<std_msgs::msg::Bool>(serialize("std_msgs/msg/Bool", "{data: true}"));
  const auto int_message = deserialize<std_msgs::msg::Int32>(serialize("std_msgs/msg/Int32", "{data: -42}"));

  EXPECT_TRUE(bool_message.data);
  EXPECT_EQ(int_message.data, -42);
}

TEST(YamlMessageTest, SerializesWideIntegerFields) {
  const auto signed_message = deserialize<std_msgs::msg::Int64>(serialize("std_msgs/msg/Int64", "{data: -9000000000}"));
  const auto unsigned_message =
      deserialize<std_msgs::msg::UInt64>(serialize("std_msgs/msg/UInt64", "{data: 18000000000}"));

  EXPECT_EQ(signed_message.data, -9000000000LL);
  EXPECT_EQ(unsigned_message.data, 18000000000ULL);
}

TEST(YamlMessageTest, SerializesFloatField) {
  const auto message = deserialize<std_msgs::msg::Float32>(serialize("std_msgs/msg/Float32", "{data: 1.5}"));

  EXPECT_FLOAT_EQ(message.data, 1.5F);
}

TEST(YamlMessageTest, SerializesByteField) {
  const auto message = deserialize<std_msgs::msg::Byte>(serialize("std_msgs/msg/Byte", "{data: 7}"));

  EXPECT_EQ(message.data, 7U);
}

TEST(YamlMessageTest, SerializesWideStringField) {
  const auto message =
      deserialize<test_msgs::msg::WStrings>(serialize("test_msgs/msg/WStrings", "{wstring_value: hello}"));

  EXPECT_EQ(message.wstring_value, u"hello");
}

TEST(YamlMessageTest, SerializesWideStringSequence) {
  const auto message = deserialize<test_msgs::msg::WStrings>(
      serialize("test_msgs/msg/WStrings", "{unbounded_sequence_of_wstrings: [foo, bar]}"));

  ASSERT_EQ(message.unbounded_sequence_of_wstrings.size(), 2U);
  EXPECT_EQ(message.unbounded_sequence_of_wstrings[0], u"foo");
  EXPECT_EQ(message.unbounded_sequence_of_wstrings[1], u"bar");
}

TEST(YamlMessageTest, SerializesBoundedString) {
  const auto message =
      deserialize<test_msgs::msg::Strings>(serialize("test_msgs/msg/Strings", "{bounded_string_value: within bound}"));

  EXPECT_EQ(message.bounded_string_value, "within bound");
}

TEST(YamlMessageTest, RejectsBoundedStringOverflow) {
  // bounded_string_value is declared as string<=22; 23 characters overflows.
  expectFailure("test_msgs/msg/Strings", "{bounded_string_value: aaaaaaaaaaaaaaaaaaaaaaa}");
}

TEST(YamlMessageTest, SerializesBoundedSequence) {
  const auto message = deserialize<test_msgs::msg::BoundedSequences>(
      serialize("test_msgs/msg/BoundedSequences", "{int32_values: [10, 20, 30]}"));

  ASSERT_EQ(message.int32_values.size(), 3U);
  EXPECT_EQ(message.int32_values[0], 10);
  EXPECT_EQ(message.int32_values[2], 30);
}

TEST(YamlMessageTest, RejectsBoundedSequenceOverflow) {
  // int32_values is declared as int32[<=3]; a fourth entry exceeds the bound.
  expectFailure("test_msgs/msg/BoundedSequences", "{int32_values: [1, 2, 3, 4]}");
}

TEST(YamlMessageTest, SerializesUnboundedSequence) {
  const auto message =
      deserialize<std_msgs::msg::UInt8MultiArray>(serialize("std_msgs/msg/UInt8MultiArray", "{data: [1, 2, 3]}"));

  ASSERT_EQ(message.data.size(), 3U);
  EXPECT_EQ(message.data[0], 1U);
  EXPECT_EQ(message.data[1], 2U);
  EXPECT_EQ(message.data[2], 3U);
}

TEST(YamlMessageTest, SerializesFixedArray) {
  const auto message = deserialize<sensor_msgs::msg::Imu>(
      serialize("sensor_msgs/msg/Imu", "{orientation_covariance: [1, 2, 3, 4, 5, 6, 7, 8, 9]}"));

  ASSERT_EQ(message.orientation_covariance.size(), 9U);
  EXPECT_DOUBLE_EQ(message.orientation_covariance[0], 1.0);
  EXPECT_DOUBLE_EQ(message.orientation_covariance[8], 9.0);
}

TEST(YamlMessageTest, SerializesSequenceOfMessages) {
  const auto message = deserialize<nav_msgs::msg::Path>(serialize("nav_msgs/msg/Path",
                                                                  "{poses: [{pose: {position: {x: 1.0}}}, "
                                                                  "{pose: {position: {x: 2.0}}}]}"));

  ASSERT_EQ(message.poses.size(), 2U);
  EXPECT_DOUBLE_EQ(message.poses[0].pose.position.x, 1.0);
  EXPECT_DOUBLE_EQ(message.poses[1].pose.position.x, 2.0);
}

TEST(YamlMessageTest, RejectsMalformedYaml) { expectFailure("std_msgs/msg/String", "{data: ["); }

TEST(YamlMessageTest, RejectsUnknownField) { expectFailure("std_msgs/msg/String", "{missing: hello}"); }

TEST(YamlMessageTest, RejectsWrongScalarType) { expectFailure("std_msgs/msg/Int32", "{data: not-an-integer}"); }

TEST(YamlMessageTest, RejectsIntegerOutOfRange) { expectFailure("std_msgs/msg/Int8", "{data: 9999}"); }

TEST(YamlMessageTest, RejectsFloatOutOfRange) { expectFailure("std_msgs/msg/Float32", "{data: 1.0e40}"); }

TEST(YamlMessageTest, RejectsNonMapForMessage) { expectFailure("std_msgs/msg/String", "hello"); }

TEST(YamlMessageTest, RejectsNonSequenceForArray) { expectFailure("std_msgs/msg/UInt8MultiArray", "{data: 5}"); }

TEST(YamlMessageTest, RejectsWrongFixedArrayLength) {
  expectFailure("sensor_msgs/msg/Imu", "{orientation_covariance: [1, 2, 3]}");
}

TEST(YamlMessageTest, RejectsEmptyPayload) { expectFailure("std_msgs/msg/String", ""); }

TEST(YamlMessageTest, RejectsOversizedPayload) {
  // Payloads larger than the cap are rejected before any YAML parsing or
  // message allocation occurs.
  const std::string payload(ros2_cli::kMaxYamlPayloadBytes + 1U, 'a');
  expectFailure("std_msgs/msg/String", payload);
}

TEST(YamlMessageTest, RejectsOversizedResizableSequence) {
  // A resizable sequence longer than the cap is rejected before the underlying
  // storage is grown. The compact "0," encoding keeps the payload itself well
  // under kMaxYamlPayloadBytes so the sequence-length guard is what trips.
  std::string payload = "{data: [";
  payload.reserve((ros2_cli::kMaxResizableSequenceLength + 2U) * 2U + 16U);
  for (std::size_t index = 0; index <= ros2_cli::kMaxResizableSequenceLength; ++index) {
    payload += "0,";
  }
  payload += "0]}";

  expectFailure("std_msgs/msg/UInt8MultiArray", payload);
}

TEST(YamlMessageTest, RejectsUnknownInterfaceType) { expectFailure("missing_msgs/msg/Nope", "{data: hello}"); }

// ---------------------------------------------------------------------------
// Direct coverage of the leaf scalar converters in ros2_cli::detail. These hold
// the value-level parsing and range-checking logic and are unit tested against
// raw YAML nodes, independent of ROS type introspection.
// ---------------------------------------------------------------------------

using ros2_cli::detail::checkedChar;
using ros2_cli::detail::checkedFloat;
using ros2_cli::detail::checkedInteger;
using ros2_cli::detail::checkedString;
using ros2_cli::detail::checkedU16String;
using ros2_cli::detail::checkedWChar;

YAML::Node node(const std::string& text) { return YAML::Load(text); }

TEST(YamlScalarTest, CheckedIntegerParsesInRange) {
  std::string error;
  EXPECT_EQ(checkedInteger<std::int32_t>(node("42"), "f", error), 42);
  EXPECT_EQ(checkedInteger<std::int8_t>(node("-128"), "f", error), -128);
  EXPECT_EQ(checkedInteger<std::uint8_t>(node("255"), "f", error), 255U);
}

TEST(YamlScalarTest, CheckedIntegerRejectsSignedOverflow) {
  std::string error;
  EXPECT_FALSE(checkedInteger<std::int8_t>(node("9999"), "f", error));
  EXPECT_FALSE(checkedInteger<std::int8_t>(node("-9999"), "f", error));
}

TEST(YamlScalarTest, CheckedIntegerRejectsUnsignedOverflow) {
  std::string error;
  EXPECT_FALSE(checkedInteger<std::uint8_t>(node("256"), "f", error));
}

TEST(YamlScalarTest, CheckedIntegerRejectsNonInteger) {
  std::string error;
  EXPECT_FALSE(checkedInteger<std::int32_t>(node("not-an-integer"), "f", error));
}

TEST(YamlScalarTest, CheckedFloatParsesInRange) {
  std::string error;
  EXPECT_FLOAT_EQ(checkedFloat<float>(node("1.5"), "f", error).value(), 1.5F);
  EXPECT_DOUBLE_EQ(checkedFloat<double>(node("-2.25"), "f", error).value(), -2.25);
}

TEST(YamlScalarTest, CheckedFloatRejectsOutOfRange) {
  std::string error;
  EXPECT_FALSE(checkedFloat<float>(node("1.0e40"), "f", error));
  EXPECT_FALSE(checkedFloat<float>(node("-1.0e40"), "f", error));
}

TEST(YamlScalarTest, CheckedFloatRejectsNonNumeric) {
  std::string error;
  EXPECT_FALSE(checkedFloat<double>(node("abc"), "f", error));
}

TEST(YamlScalarTest, CheckedStringRespectsUpperBound) {
  std::string error;
  EXPECT_EQ(checkedString(node("hello"), 0U, "f", error), "hello");
  EXPECT_EQ(checkedString(node("abc"), 3U, "f", error), "abc");
  EXPECT_FALSE(checkedString(node("abcd"), 3U, "f", error));
}

TEST(YamlScalarTest, CheckedU16StringWidensCodeUnits) {
  std::string error;
  const auto value = checkedU16String(node("AB"), 0U, "f", error);
  ASSERT_TRUE(value.has_value());
  ASSERT_EQ(value->size(), 2U);
  EXPECT_EQ((*value)[0], u'A');
  EXPECT_EQ((*value)[1], u'B');
  EXPECT_FALSE(checkedU16String(node("abcd"), 3U, "f", error));
}

TEST(YamlScalarTest, CheckedCharAcceptsCharacterOrCode) {
  std::string error;
  EXPECT_EQ(checkedChar(node("a"), "f", error), 'a');
  EXPECT_EQ(checkedChar(node("65"), "f", error), 'A');
}

TEST(YamlScalarTest, CheckedCharRejectsInvalid) {
  std::string error;
  EXPECT_FALSE(checkedChar(node("ab"), "f", error));
  EXPECT_FALSE(checkedChar(node("9999"), "f", error));
}

TEST(YamlScalarTest, CheckedWCharAcceptsCharacterOrCode) {
  std::string error;
  EXPECT_EQ(checkedWChar(node("a"), "f", error), u'a');
  EXPECT_EQ(checkedWChar(node("300"), "f", error), static_cast<char16_t>(300));
}

} // namespace
} // namespace ros2_livekit_bridge
