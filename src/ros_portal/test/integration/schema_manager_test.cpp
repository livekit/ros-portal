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
#include <livekit/data_track_options.h>
#include <livekit/local_data_track.h>
#include <livekit/local_participant.h>
#include <livekit/room.h>

#include <atomic>
#include <cstdint>
#include <geometry_msgs/msg/twist.hpp>
#include <optional>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <string>
#include <utility>
#include <vector>

#include "ros_portal/schema/manager.hpp"
#include "ros_portal_e2e_fixture.hpp"

namespace ros_portal::test {
namespace {

// Capture production-rendered schema text without publishing through a ROS Portal node,
// so inbound tests can create independent, preexisting, or malformed LiveKit
// tracks without exercising unrelated outbound behavior.
std::optional<std::string> renderSchemaText(const std::string& topic_type) {
  SchemaManager::LiveKitMethods methods;
  std::optional<std::string> schema_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string& text) {
    schema_text = text;
    return true;
  };
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  SchemaManager manager(std::move(methods));
  if (!manager.ensureSchemaDefined(topic_type)) {
    return std::nullopt;
  }
  return schema_text;
}

// Case: An inbound LiveKit track with the locally installed ROS schema should create a publisher on the receiving ROS
// graph. Event sequence:
// 1. Define the schema before publishing the track
// 2. Publish the inbound track
// 3. Verify the publisher is created on the receiving ROS graph
TEST_F(RosPortalTestE2E, InboundTrackCreatesPublisher) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  constexpr const char* kTopic = "/ros_portal/schema_match";
  initializeInboundOnlyRuntime(kTopic);
  auto subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [](const std_msgs::msg::String::ConstSharedPtr&) {});
  ASSERT_NE(subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return topicExists(*robotBNode(), kTopic); }, kGraphTimeout));

  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  // Define the schema before publishing the track
  const auto schema_text = renderSchemaText("std_msgs/msg/String");
  ASSERT_TRUE(schema_text.has_value()) << "std_msgs/msg/String schema was unavailable";
  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_TRUE(publisher->defineSchema(schema_id, *schema_text));

  // Publish the inbound track
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result) << track_result.error().message;

  EXPECT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));
}

// Case: A browser teleoperation client should control a ROS participant by publishing JSON frames with a ROS type name,
// without defining an external schema document. JSON frames must advertise a JsonSchema schema encoding; the SDK
// rejects any other pairing at publish time. Event sequence:
// 1. Start the receiving ROS Portal node and create a typed ROS subscription.
// 2. Connect an independent LiveKit publisher without defining a schema document.
// 3. Publish the same cmd_vel data track metadata used by the web teleoperation client.
// 4. Push a Twist-shaped JSON frame and verify the typed ROS message contents.
TEST_F(RosPortalTestE2E, InboundWebJsonControlFrameCreatesTypedRosMessage) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  constexpr const char* kTrackName = "cmd_vel";
  constexpr const char* kTopic = "/cmd_vel";
  constexpr const char* kType = "geometry_msgs/msg/Twist";

  // Start the receiving ROS Portal node and subscribe to the expected typed ROS message.
  initializeInboundOnlyRuntime(kTopic);

  std::atomic_bool received{false};
  auto subscription = robotBNode()->create_subscription<geometry_msgs::msg::Twist>(
      kTopic, 10, [&received](const geometry_msgs::msg::Twist::ConstSharedPtr& msg) {
        if (msg->linear.x == 1.25 && msg->linear.y == -2.5 && msg->angular.z == 0.75) {
          received.store(true);
        }
      });
  ASSERT_NE(subscription, nullptr);

  // Connect an independent participant that will publish the LiveKit data track as JSON.
  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  // Match the web client: the schema ID supplies the locally installed ROS type,
  // but the publisher does not define or upload an external schema document.
  const livekit::DataTrackSchemaId schema_id{kType, livekit::DataTrackSchemaEncoding::JsonSchema};

  // Match useControlCmdTrack(): publish a relative cmd_vel track with JSON frames.
  livekit::DataTrackPublishOptions options;
  options.name = kTrackName;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Json;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result) << track_result.error().message;

  // Wait for ROS Portal to validate the track and create its ROS publisher.
  ASSERT_TRUE(waitFor([&]() { return subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  // Push the JSON frame until the subscription verifies the translated typed message.
  const std::string json = R"({"linear":{"x":1.25,"y":-2.5,"z":0.0},"angular":{"x":0.0,"y":0.0,"z":0.75}})";
  const std::vector<std::uint8_t> payload(json.begin(), json.end());
  EXPECT_TRUE(waitFor(
      [&]() {
        if (!received.load()) {
          (void)track_result.value()->tryPush(std::vector<std::uint8_t>(payload));
        }
        return received.load();
      },
      kMessageTimeout));
}

// Case: A receiving ROS Portal node should discover a preexisting LiveKit track and forward it when a matching ROS
// subscriber appears later. Event sequence:
// 1. Publish a valid-schema LiveKit track before the receiving ROS Portal node starts.
// 2. Start ROS Portal and verify it creates a ROS publisher for that track.
// 3. Add the matching ROS subscription after the track has been discovered.
// 4. Push a serialized frame and verify it reaches the late ROS subscriber.
TEST_F(RosPortalTestE2E, AcceptsTrackBeforeRosSubscriber) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  constexpr const char* kTopic = "/ros_portal/schema_before_subscriber";

  // Connect an independent publisher before the receiving ROS Portal node exists.
  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  // Define the schema before publishing the track
  const auto schema_text = renderSchemaText("std_msgs/msg/String");
  ASSERT_TRUE(schema_text.has_value()) << "std_msgs/msg/String schema was unavailable";
  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_TRUE(publisher->defineSchema(schema_id, *schema_text));

  // Publish the inbound track
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result) << track_result.error().message;

  // Start ROS Portal and confirm it discovers the preexisting track.
  initializeInboundOnlyRuntime(kTopic);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  // Add the local ROS subscriber only after track discovery.
  std::atomic_bool received{false};
  auto late_subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&received](const std_msgs::msg::String::ConstSharedPtr& msg) {
        if (msg->data == "published before local subscriber") {
          received.store(true);
        }
      });
  ASSERT_NE(late_subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return late_subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  // Serialize a ROS message into the track's CDR frame encoding.
  std_msgs::msg::String message;
  message.data = "published before local subscriber";
  const rclcpp::Serialization<std_msgs::msg::String> serializer;
  rclcpp::SerializedMessage serialized;
  serializer.serialize_message(&message, &serialized);
  const auto& raw = serialized.get_rcl_serialized_message();
  const std::vector<std::uint8_t> payload(raw.buffer, raw.buffer + raw.buffer_length);

  // Push the frame until the late ROS subscriber receives it.
  EXPECT_TRUE(waitFor(
      [&]() {
        if (!received.load()) {
          (void)track_result.value()->tryPush(std::vector<std::uint8_t>(payload));
        }
        return received.load();
      },
      kMessageTimeout));
}

// Case: A track whose advertised ROS type has different schema text should be rejected even when no matching local ROS
// subscriber exists.
// 1. Start an inbound ROS Portal node without creating a local ROS subscriber.
// 2. Define incorrect schema text under the expected ROS type.
// 3. Publish a LiveKit track that advertises the mismatched schema.
// 4. Verify ROS Portal does not create a ROS publisher for the track.
TEST_F(RosPortalTestE2E, RejectsTrackSchemaMismatchNoSubscriber) {
  constexpr const char* kTopic = "/ros_portal/schema_mismatch";

  // Start the receiver with no local subscriber for the allowed topic.
  initializeInboundOnlyRuntime(kTopic);

  // Connect an independent publisher to ROS Portal's LiveKit room.
  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  // Publish a track whose schema ID claims String but whose text defines int32.
  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_TRUE(publisher->defineSchema(schema_id, "int32 data\n"));
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result) << track_result.error().message;

  // Confirm schema validation prevents creation of a ROS publisher.
  EXPECT_FALSE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kNegativeAssertionTimeout));
}

} // namespace
} // namespace ros_portal::test
