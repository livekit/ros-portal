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
#include <optional>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <string>
#include <utility>
#include <vector>

#include "bridge_e2e_fixture.hpp"
#include "ros2_livekit_bridge/schema_manager.hpp"

namespace ros2_livekit_bridge::test {
namespace {

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

// End-to-end bridge check: two isolated ROS graphs publish message topics through
// separate bridge participants in the same LiveKit room, then verify each graph
// receives the other's message only via the bridge. This catches regressions in
// LiveKit data-track forwarding and ROS graph isolation without relying on
// shared local ROS discovery.
TEST_F(BridgeTestE2E, RepublishesRosMessagesBothWays) {
  initializeRuntime(kBidirectionalTopic);

  EXPECT_TRUE(
      verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, kBidirectionalTopic, "message from bridge a"));
  EXPECT_TRUE(
      verifyDirection(publisherB(), robotANode(), kBidirectionalTopic, kBidirectionalTopic, "message from bridge b"));
}

// With preserve_id enabled on the receiver, an inbound data track is
// republished under a topic prefixed with the publishing participant's
// sanitized identity, e.g. /bridge/out from participant bridge-test-a becomes
// /bridge_test_a/bridge/out.
TEST_F(BridgeTestE2E, PreserveIdPrefixesInboundTopicWithPublisherIdentity) {
  // Only bridge B (the A->B receiver) opts into preserve_id.
  initializeRuntime(kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic,
                    /*preserve_id_a=*/false, /*preserve_id_b=*/true);

  const auto prefix = utils::sanitizeRosNameToken(identityA());
  ASSERT_TRUE(prefix.has_value());
  const std::string expected_inbound_topic = "/" + *prefix + kBidirectionalTopic;

  EXPECT_TRUE(verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, expected_inbound_topic,
                              "message from bridge a"));
}

TEST_F(BridgeTestE2E, DoesNotRepublishTopicNotAllowedOnReceiver) {
  constexpr const char* kSenderOnlyTopic = "/bridge/sender_only";
  constexpr const char* kReceiverAllowedTopic = "/bridge/receiver_allowed";

  initializeRuntime(kSenderOnlyTopic, kReceiverAllowedTopic, kSenderOnlyTopic, kReceiverAllowedTopic);

  EXPECT_TRUE(verifyDirectionNotForwarded(publisherA(), robotBNode(), kSenderOnlyTopic, kSenderOnlyTopic,
                                          "message that should stay blocked"));
}

TEST_F(BridgeTestE2E, AcceptsInboundTrackWithExactSchema) {
  constexpr const char* kTopic = "/bridge/schema_match";
  initializeInboundOnlyRuntime(kTopic);
  auto subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [](const std_msgs::msg::String::ConstSharedPtr&) {});
  ASSERT_NE(subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return topicExists(*robotBNode(), kTopic); }, kGraphTimeout));

  const auto schema_text = renderSchemaText("std_msgs/msg/String");
  if (!schema_text.has_value()) {
    FAIL() << "std_msgs/msg/String schema was unavailable";
    return;
  }

  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, *schema_text));
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  EXPECT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));
}

TEST_F(BridgeTestE2E, AcceptsPreexistingInboundTrackBeforeLocalRosEndpointAppears) {
  constexpr const char* kTopic = "/bridge/schema_before_endpoint";
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  const auto schema_text = renderSchemaText("std_msgs/msg/String");
  if (!schema_text.has_value()) {
    FAIL() << "std_msgs/msg/String schema was unavailable";
    return;
  }

  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, *schema_text));
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  // Start the receiving bridge only after the remote track exists. The bridge
  // must handle the connect-time track event, validate from its installed
  // interface definition, and create a publisher without a local endpoint.
  initializeInboundOnlyRuntime(kTopic);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  std::atomic_bool received{false};
  auto late_subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&received](const std_msgs::msg::String::ConstSharedPtr& msg) {
        if (msg->data == "published before local endpoint") {
          received.store(true);
        }
      });
  ASSERT_NE(late_subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return late_subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  std_msgs::msg::String message;
  message.data = "published before local endpoint";
  const rclcpp::Serialization<std_msgs::msg::String> serializer;
  rclcpp::SerializedMessage serialized;
  serializer.serialize_message(&message, &serialized);
  const auto& raw = serialized.get_rcl_serialized_message();
  const std::vector<std::uint8_t> payload(raw.buffer, raw.buffer + raw.buffer_length);

  EXPECT_TRUE(waitFor(
      [&]() {
        if (!received.load()) {
          (void)track_result.value()->tryPush(std::vector<std::uint8_t>(payload));
        }
        return received.load();
      },
      kMessageTimeout));
}

TEST_F(BridgeTestE2E, RejectsInboundTrackWithDifferentSchemaWithoutLocalEndpoint) {
  constexpr const char* kTopic = "/bridge/schema_mismatch";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  ASSERT_TRUE(publisher_room.connect(liveKitUrl(), tokenA(), room_options));
  const auto publisher = publisher_room.localParticipant().lock();
  ASSERT_NE(publisher, nullptr);

  const livekit::DataTrackSchemaId schema_id{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  ASSERT_NO_THROW(publisher->defineSchema(schema_id, "int32 data\n"));
  livekit::DataTrackPublishOptions options;
  options.name = kTopic;
  options.schema = schema_id;
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  const auto track_result = publisher->publishDataTrack(options);
  ASSERT_TRUE(track_result);

  EXPECT_FALSE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kNegativeAssertionTimeout));
}

} // namespace
} // namespace ros2_livekit_bridge::test
