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

#include <gtest/gtest.h>

#include <cstddef>
#include <geometry_msgs/msg/twist.hpp>
#include <nlohmann/json.hpp>
#include <nav_msgs/msg/path.hpp>
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

#include "ros2_livekit_bridge/cli/constants.hpp"
#include "ros2_livekit_bridge/introspection/introspection_utils.hpp"

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
  auto serialized = introspection::serializedMessageFromYaml(msg_type, payload, error);
  EXPECT_TRUE(serialized.has_value()) << error;
  return serialized.value_or(rclcpp::SerializedMessage{});
}

// Asserts conversion fails and reports a non-empty diagnostic message.
void expectFailure(const std::string& msg_type, const std::string& payload) {
  std::string error;
  const auto serialized = introspection::serializedMessageFromYaml(msg_type, payload, error);
  EXPECT_FALSE(serialized.has_value());
  EXPECT_FALSE(error.empty());
}

// Asserts conversion succeeds without reporting an error. Used for payloads the
// medkit serialization path accepts leniently (it does not enforce message-IDL
// constraints such as bounded-string length, integer range, unknown-field
// rejection, or scalar-vs-sequence shape); see the AcceptsLenient* tests below.
void expectAccepted(const std::string& msg_type, const std::string& payload) {
  std::string error;
  const auto serialized = introspection::serializedMessageFromYaml(msg_type, payload, error);
  EXPECT_TRUE(serialized.has_value()) << error;
  EXPECT_TRUE(error.empty()) << error;
}

rclcpp::SerializedMessage serializeJson(const std::string& msg_type, const std::string& payload) {
  std::string error;
  auto serialized = introspection::serializedMessageFromJson(msg_type, payload, error);
  EXPECT_TRUE(serialized.has_value()) << error;
  return serialized.value_or(rclcpp::SerializedMessage{});
}

void expectJsonFailure(const std::string& msg_type, const std::string& payload) {
  std::string error;
  const auto serialized = introspection::serializedMessageFromJson(msg_type, payload, error);
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

TEST(YamlMessageTest, PopulatesExistingMessageFromYaml) {
  std_msgs::msg::String message;
  std::string error;

  EXPECT_TRUE(introspection::populateMessageFromYaml("std_msgs/msg/String", "{data: hello}", &message, error)) << error;

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

TEST(YamlMessageTest, AcceptsLenientBoundedStringOverflow) {
  // bounded_string_value is declared as string<=22; 23 characters overflow the
  // IDL bound. The medkit serialization path does not enforce string bounds, so
  // the payload is accepted and the full value is carried through.
  const auto message = deserialize<test_msgs::msg::Strings>(
      serialize("test_msgs/msg/Strings", "{bounded_string_value: aaaaaaaaaaaaaaaaaaaaaaa}"));

  EXPECT_EQ(message.bounded_string_value, "aaaaaaaaaaaaaaaaaaaaaaa");
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

TEST(YamlMessageTest, AcceptsLenientUnknownField) {
  // The serialization path walks the message type's members and skips any that
  // are absent from the payload; fields present in the payload but unknown to
  // the type are simply never read. The unknown key is ignored and known fields
  // retain their defaults.
  const auto message = deserialize<std_msgs::msg::String>(serialize("std_msgs/msg/String", "{missing: hello}"));

  EXPECT_EQ(message.data, "");
}

TEST(YamlMessageTest, RejectsWrongScalarType) { expectFailure("std_msgs/msg/Int32", "{data: not-an-integer}"); }

TEST(YamlMessageTest, AcceptsLenientIntegerOutOfRange) {
  // int8 has range [-128, 127]; 9999 exceeds it. The serialization path applies
  // no range check and narrows the value via the YAML scalar conversion, so the
  // payload is accepted rather than rejected. The narrowed result is defined by
  // yaml-cpp's conversion and intentionally not asserted here.
  expectAccepted("std_msgs/msg/Int8", "{data: 9999}");
}

TEST(YamlMessageTest, RejectsFloatOutOfRange) { expectFailure("std_msgs/msg/Float32", "{data: 1.0e40}"); }

TEST(YamlMessageTest, RejectsNonMapForMessage) { expectFailure("std_msgs/msg/String", "hello"); }

TEST(YamlMessageTest, AcceptsLenientNonSequenceForArray) {
  // data is an unbounded sequence. A scalar YAML node has size 0, so the
  // sequence path resizes to 0 and writes no elements rather than rejecting the
  // scalar. The payload is accepted and the sequence comes through empty.
  const auto message =
      deserialize<std_msgs::msg::UInt8MultiArray>(serialize("std_msgs/msg/UInt8MultiArray", "{data: 5}"));

  EXPECT_TRUE(message.data.empty());
}

TEST(YamlMessageTest, RejectsWrongFixedArrayLength) {
  expectFailure("sensor_msgs/msg/Imu", "{orientation_covariance: [1, 2, 3]}");
}

TEST(YamlMessageTest, RejectsEmptyPayload) { expectFailure("std_msgs/msg/String", ""); }

TEST(YamlMessageTest, RejectsOversizedPayload) {
  // Payloads larger than the cap are rejected before any YAML parsing or
  // message allocation occurs.
  const std::string payload(cli::kMaxYamlPayloadBytes + 1U, 'a');
  expectFailure("std_msgs/msg/String", payload);
}

TEST(YamlMessageTest, RejectsOversizedResizableSequence) {
  // A resizable sequence longer than the cap is rejected before the underlying
  // storage is grown. The compact "0," encoding keeps the payload itself well
  // under kMaxYamlPayloadBytes so the sequence-length guard is what trips.
  std::string payload = "{data: [";
  payload.reserve((cli::kMaxResizableSequenceLength + 2U) * 2U + 16U);
  for (std::size_t index = 0; index <= cli::kMaxResizableSequenceLength; ++index) {
    payload += "0,";
  }
  payload += "0]}";

  expectFailure("std_msgs/msg/UInt8MultiArray", payload);
}

TEST(YamlMessageTest, RejectsUnknownInterfaceType) { expectFailure("missing_msgs/msg/Nope", "{data: hello}"); }

TEST(JsonMessageTest, SerializesNestedMessage) {
  const auto serialized = serializeJson("geometry_msgs/msg/Twist",
                                        R"({"linear":{"x":0.5,"y":0.0,"z":0.0},"angular":{"x":0.0,"y":0.0,"z":1.25}})");

  const auto message = deserialize<geometry_msgs::msg::Twist>(serialized);
  EXPECT_DOUBLE_EQ(message.linear.x, 0.5);
  EXPECT_DOUBLE_EQ(message.angular.z, 1.25);
}

TEST(JsonMessageTest, RejectsMalformedJson) { expectJsonFailure("std_msgs/msg/String", R"({"data":)"); }

TEST(JsonMessageTest, RejectsNonObjectRoot) { expectJsonFailure("std_msgs/msg/String", R"("hello")"); }

TEST(JsonMessageTest, RejectsIncompatibleFieldType) {
  expectJsonFailure("std_msgs/msg/Int32", R"({"data":"not-an-integer"})");
}

TEST(JsonMessageTest, RejectsOversizedPayload) {
  const std::string payload(cli::kMaxYamlPayloadBytes + 1U, ' ');
  expectJsonFailure("std_msgs/msg/String", payload);
}

TEST(JsonMessageTest, RejectsOversizedResizableSequence) {
  std::string payload = R"({"data":[)";
  payload.reserve((cli::kMaxResizableSequenceLength + 2U) * 2U + 16U);
  for (std::size_t index = 0; index <= cli::kMaxResizableSequenceLength; ++index) {
    payload += "0,";
  }
  payload += "0]}";

  expectJsonFailure("std_msgs/msg/UInt8MultiArray", payload);
}

// ---------------------------------------------------------------------------
// Outbound helpers: jsonFromSerializedMessage (CDR -> JSON) and renderJsonSchema.
// ---------------------------------------------------------------------------

TEST(JsonOutboundTest, RoundTripsThroughJsonAndBack) {
  // Serialize a message to CDR, convert the CDR to JSON, then feed that JSON
  // back through the inbound path and confirm the original fields survive.
  const auto cdr =
      serializeJson("geometry_msgs/msg/Twist", R"({"linear":{"x":0.5,"y":0.0,"z":0.0},"angular":{"x":0.0,"y":0.0,"z":1.25}})");

  std::string error;
  const auto json = introspection::jsonFromSerializedMessage("geometry_msgs/msg/Twist", cdr, error);
  ASSERT_TRUE(json.has_value()) << error;
  EXPECT_TRUE(error.empty()) << error;

  const auto reserialized = serializeJson("geometry_msgs/msg/Twist", *json);
  const auto message = deserialize<geometry_msgs::msg::Twist>(reserialized);
  EXPECT_DOUBLE_EQ(message.linear.x, 0.5);
  EXPECT_DOUBLE_EQ(message.angular.z, 1.25);
}

TEST(JsonOutboundTest, JsonConversionFailsForUnknownType) {
  std::string error;
  const auto json = introspection::jsonFromSerializedMessage("missing_msgs/msg/Nope", rclcpp::SerializedMessage{}, error);
  EXPECT_FALSE(json.has_value());
  EXPECT_FALSE(error.empty());
}

TEST(JsonOutboundTest, RendersJsonSchemaForKnownType) {
  std::string error;
  const auto schema = introspection::renderJsonSchema("std_msgs/msg/String", error);
  ASSERT_TRUE(schema.has_value()) << error;
  EXPECT_TRUE(error.empty()) << error;

  const auto parsed = nlohmann::json::parse(*schema);
  EXPECT_EQ(parsed.at("type"), "object");
  ASSERT_TRUE(parsed.contains("properties"));
  ASSERT_TRUE(parsed.at("properties").contains("data"));
  EXPECT_EQ(parsed.at("properties").at("data").at("type"), "string");
}

TEST(JsonOutboundTest, RenderJsonSchemaFailsForUnknownType) {
  std::string error;
  const auto schema = introspection::renderJsonSchema("missing_msgs/msg/Nope", error);
  EXPECT_FALSE(schema.has_value());
  EXPECT_FALSE(error.empty());
}

} // namespace
} // namespace ros2_livekit_bridge
