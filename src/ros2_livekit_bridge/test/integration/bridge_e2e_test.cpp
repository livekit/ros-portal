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
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <std_msgs/msg/string.hpp>
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

livekit::DataTrackPublishOptions stringTrackOptions(const std::string& topic) {
  livekit::DataTrackPublishOptions options;
  options.name = topic;
  options.schema = livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  return options;
}

std::vector<std::uint8_t> serializeString(const std::string& text) {
  std_msgs::msg::String message;
  message.data = text;
  const rclcpp::Serialization<std_msgs::msg::String> serializer;
  rclcpp::SerializedMessage serialized;
  serializer.serialize_message(&message, &serialized);
  const auto& raw = serialized.get_rcl_serialized_message();
  return {raw.buffer, raw.buffer + raw.buffer_length};
}

class DataTrackLifecycleTests : public BridgeTestE2E {
protected:
  std::shared_ptr<livekit::LocalParticipant> connectPublisher(livekit::Room& room) {
    livekit::RoomOptions room_options;
    room_options.auto_subscribe = true;
    if (!room.connect(liveKitUrl(), tokenA(), room_options)) {
      ADD_FAILURE() << "Independent LiveKit publisher failed to connect";
      return nullptr;
    }

    auto publisher = room.localParticipant().lock();
    if (!publisher) {
      ADD_FAILURE() << "Independent LiveKit publisher was unavailable";
    }
    return publisher;
  }

  bool defineStringSchema(livekit::LocalParticipant& publisher) {
    const auto schema_text = renderSchemaText("std_msgs/msg/String");
    if (!schema_text.has_value()) {
      ADD_FAILURE() << "std_msgs/msg/String schema was unavailable";
      return false;
    }

    try {
      publisher.defineSchema(
          livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg}, *schema_text);
    } catch (const std::exception& exception) {
      ADD_FAILURE() << "Failed to define std_msgs/msg/String schema: " << exception.what();
      return false;
    }
    return true;
  }

  bool pushUntilReceived(const std::shared_ptr<livekit::LocalDataTrack>& track,
                         const std::vector<std::uint8_t>& payload, const std::atomic_bool& received) {
    return waitFor(
        [&]() {
          if (!received.load()) {
            (void)track->tryPush(std::vector<std::uint8_t>(payload));
          }
          return received.load();
        },
        kMessageTimeout);
  }
};

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

// Case: A valid track published after bridge startup but before a ROS
// subscriber appears should begin forwarding when the subscriber joins.
// 1. Start the receiving bridge without a ROS subscriber.
// 2. Publish a valid LiveKit data track.
// 3. Add the ROS subscriber after the bridge discovers the track.
// 4. Push a frame and verify the late subscriber receives it.
TEST_F(DataTrackLifecycleTests, TrackAfterBridgeBeforeSubscriberForwards) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/bridge/lifecycle/track_after_bridge";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  std::atomic_bool received{false};
  auto subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&received](const std_msgs::msg::String::ConstSharedPtr& message) {
        received.store(message->data == "late subscriber");
      });
  ASSERT_NE(subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  EXPECT_TRUE(pushUntilReceived(track_result.value(), serializeString("late subscriber"), received));
}

// Case: Unpublishing an active LiveKit track should remove its ROS publisher.
// 1. Start the bridge and publish a valid track.
// 2. Verify the bridge creates the ROS publisher.
// 3. Unpublish the LiveKit track.
// 4. Verify the ROS publisher disappears.
TEST_F(DataTrackLifecycleTests, UnpublishingTrackRemovesRosPublisher) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/bridge/lifecycle/unpublish";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  publisher->unpublishDataTrack(track_result.value());

  EXPECT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) == 0U; }, kGraphTimeout));
}

// Case: A topic should recover when its LiveKit track is republished with a new
// SID after the original track is unpublished.
// 1. Publish a valid track and verify its ROS publisher.
// 2. Unpublish it and verify cleanup.
// 3. Republish the same topic as a new track.
// 4. Verify ROS publication and message delivery resume.
TEST_F(DataTrackLifecycleTests, RepublishedTrackResumesForwarding) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/bridge/lifecycle/republish";
  initializeInboundOnlyRuntime(kTopic);

  std::atomic_bool received{false};
  auto subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&received](const std_msgs::msg::String::ConstSharedPtr& message) {
        received.store(message->data == "after republish");
      });
  ASSERT_NE(subscription, nullptr);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));

  const auto first_track = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(first_track);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));
  publisher->unpublishDataTrack(first_track.value());
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) == 0U; }, kGraphTimeout));

  const auto second_track = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(second_track);
  ASSERT_TRUE(waitFor([&]() { return subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  EXPECT_TRUE(pushUntilReceived(second_track.value(), serializeString("after republish"), received));
}

// Case: Destroying a bridge with an active inbound track should stop its reader
// and remove its ROS publisher without hanging.
// 1. Start the bridge and publish a valid track.
// 2. Verify the ROS publisher exists.
// 3. Destroy the bridge while the track remains active.
// 4. Verify teardown completes and the ROS publisher disappears.
TEST_F(DataTrackLifecycleTests, BridgeShutdownCleansUpActiveTrack) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/bridge/lifecycle/bridge_shutdown";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  const auto start = std::chrono::steady_clock::now();
  shutdownBridgeB();
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, kGraphTimeout);
  EXPECT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) == 0U; }, kGraphTimeout));
}

// Case: An active inbound track should continue serving ROS subscribers that
// leave and later rejoin.
// 1. Publish a valid track and deliver a frame to the first subscriber.
// 2. Destroy the subscriber while keeping the track active.
// 3. Create a replacement subscriber.
// 4. Push another frame and verify the replacement receives it.
TEST_F(DataTrackLifecycleTests, RecreatedSubscriberReceivesActiveTrack) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/bridge/lifecycle/subscriber_recreated";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result);
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  std::atomic_bool first_received{false};
  auto first_subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&first_received](const std_msgs::msg::String::ConstSharedPtr& message) {
        first_received.store(message->data == "first subscriber");
      });
  ASSERT_NE(first_subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return first_subscription->get_publisher_count() > 0U; }, kGraphTimeout));
  ASSERT_TRUE(pushUntilReceived(track_result.value(), serializeString("first subscriber"), first_received));

  first_subscription.reset();
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_subscribers(kTopic) == 0U; }, kGraphTimeout));
  ASSERT_GT(robotBNode()->count_publishers(kTopic), 0U);

  std::atomic_bool second_received{false};
  auto second_subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kTopic, 10, [&second_received](const std_msgs::msg::String::ConstSharedPtr& message) {
        second_received.store(message->data == "second subscriber");
      });
  ASSERT_NE(second_subscription, nullptr);
  ASSERT_TRUE(waitFor([&]() { return second_subscription->get_publisher_count() > 0U; }, kGraphTimeout));

  EXPECT_TRUE(pushUntilReceived(track_result.value(), serializeString("second subscriber"), second_received));
}

} // namespace
} // namespace ros2_livekit_bridge::test
