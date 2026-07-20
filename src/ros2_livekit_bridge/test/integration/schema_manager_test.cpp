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

#include "ros2_livekit_bridge/schema_manager.hpp"

#include <gtest/gtest.h>
#include <livekit/data_track_options.h>
#include <livekit/local_data_track.h>
#include <livekit/local_participant.h>
#include <livekit/room.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <string>
#include <utility>
#include <vector>

#include "bridge_e2e_fixture.hpp"

namespace ros2_livekit_bridge::test {
namespace {

// Capture production-rendered schema text without publishing through a bridge,
// so inbound tests can create independent, preexisting, or malformed LiveKit
// tracks without exercising unrelated outbound behavior.
std::optional<std::string> renderSchemaText(const std::string& topic_type) {
  SchemaManager::LiveKitMethods methods;
  std::optional<std::string> schema_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string& text) {
    schema_text = text;
    return livekit::Result<void, std::string>::success();
  };
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return livekit::Result<std::string, std::string>::failure("unused");
  };
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
TEST_F(BridgeTestE2E, InboundTrackCreatesPublisher) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  constexpr const char* kTopic = "/bridge/schema_match";
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
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, *schema_text));

  // Publish the inbound track
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  EXPECT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));
}

// Case: A receiving bridge should discover a preexisting LiveKit track and forward it when a matching ROS subscriber
// appears later. Event sequence:
// 1. Publish a valid-schema LiveKit track before the receiving bridge starts.
// 2. Start the bridge and verify it creates a ROS publisher for that track.
// 3. Add the matching ROS subscription after the track has been discovered.
// 4. Push a serialized frame and verify it reaches the late ROS subscriber.
TEST_F(BridgeTestE2E, AcceptsTrackBeforeRosSubscriber) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  constexpr const char* kTopic = "/bridge/schema_before_subscriber";

  // Connect an independent publisher before the receiving bridge exists.
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
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, *schema_text));

  // Publish the inbound track
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  // Start the bridge and confirm it discovers the preexisting track.
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
// 1. Start an inbound bridge without creating a local ROS subscriber.
// 2. Define incorrect schema text under the expected ROS type.
// 3. Publish a LiveKit track that advertises the mismatched schema.
// 4. Verify the bridge does not create a ROS publisher for the track.
TEST_F(BridgeTestE2E, RejectsTrackSchemaMismatchNoSubscriber) {
  constexpr const char* kTopic = "/bridge/schema_mismatch";

  // Start the receiver with no local subscriber for the allowed topic.
  initializeInboundOnlyRuntime(kTopic);

  // Connect an independent publisher to the bridge's LiveKit room.
  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  // Publish a track whose schema ID claims String but whose text defines int32.
  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, "int32 data\n"));
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  // Confirm schema validation prevents creation of a ROS publisher.
  EXPECT_FALSE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kNegativeAssertionTimeout));
}

} // namespace
} // namespace ros2_livekit_bridge::test
