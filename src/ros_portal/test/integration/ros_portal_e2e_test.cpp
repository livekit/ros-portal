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
#include <livekit/remote_participant.h>
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

#include "ros_portal/schema/manager.hpp"
#include "ros_portal_e2e_fixture.hpp"

namespace ros_portal::test {
namespace {

TEST_F(RosPortalTestE2E, AdvertisesRobotParticipantAttribute) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  initializeInboundOnlyRuntime("/participant_attributes");

  livekit::Room observer_room;
  const livekit::RoomOptions room_options;
  ASSERT_TRUE(observer_room.connect(liveKitUrl(), tokenA(), room_options));

  // ROS Portal sets kRobotParticipantAttribute after connect; the observer may join first or receive
  // the attribute update asynchronously via participant metadata sync.
  std::optional<std::string> attribute_value;
  ASSERT_TRUE(waitFor(
      [&]() {
        const auto participant = observer_room.remoteParticipant(identityB()).lock();
        if (!participant) {
          return false;
        }
        const auto attribute = participant->attributes().find(kRobotParticipantAttribute);
        if (attribute == participant->attributes().end()) {
          return false;
        }
        attribute_value = attribute->second;
        return true;
      },
      kGraphTimeout))
      << "ROS Portal did not advertise " << kRobotParticipantAttribute << " on participant " << identityB();

  EXPECT_EQ(attribute_value.value(), "true");
}

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

class DataTrackLifecycleTests : public RosPortalTestE2E {
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
      if (!publisher.defineSchema(
              livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg},
              *schema_text)) {
        ADD_FAILURE() << "LiveKit SDK rejected the std_msgs/msg/String schema";
        return false;
      }
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
            (void)track->tryPush(payload.data(), payload.size());
          }
          return received.load();
        },
        kMessageTimeout);
  }
};

// End-to-end ROS Portal check: two isolated ROS graphs publish message topics through
// separate ROS Portal participants in the same LiveKit room, then verify each graph
// receives the other's message only via ROS Portal. This catches regressions in
// LiveKit data-track forwarding and ROS graph isolation without relying on
// shared local ROS discovery.
TEST_F(RosPortalTestE2E, RepublishesRosMessagesBothWays) {
  initializeRuntime(kBidirectionalTopic);

  EXPECT_TRUE(verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, kBidirectionalTopic,
                              "message from ROS Portal A"));
  EXPECT_TRUE(verifyDirection(publisherB(), robotANode(), kBidirectionalTopic, kBidirectionalTopic,
                              "message from ROS Portal B"));
}

// End-to-end encoding check: ROS Portal A forwards a ROS message with a configured
// outbound `encoding`, and ROS Portal B (default config) receives, decodes, and
// republishes it on its ROS graph with identical data. Covers the round trip
// for CDR (`ros2msg`) and JSON (`jsonschema`) frames.
//
// `ros2idl` is not exercised here: std_msgs/String renders as ros2msg locally,
// so an explicit ros2idl request is intentionally skipped by ROS Portal (see
// SchemaManagerTest.SkipsRos2IdlWhenLocalDefinitionIsNotIdl and
// DefinesRos2IdlWhenRequestedAndAvailable for that coverage).
class EncodingE2E : public RosPortalTestE2E, public ::testing::WithParamInterface<std::string> {};

TEST_P(EncodingE2E, ForwardsAndDecodesWithConfiguredEncoding) {
  const std::string encoding = GetParam();
  // Only ROS Portal A (the sender) sets the encoding; ROS Portal B stays on the default
  // and auto-detects the inbound frame encoding.
  initializeRuntime(kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic,
                    /*preserve_id_a=*/false, /*preserve_id_b=*/false, /*encoding_a=*/encoding, /*encoding_b=*/"");

  EXPECT_TRUE(
      verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, kBidirectionalTopic, "message via " + encoding));
}

INSTANTIATE_TEST_SUITE_P(Encodings, EncodingE2E, ::testing::Values("ros2msg", "jsonschema"),
                         [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

// With preserve_id enabled on the receiver, an inbound data track is
// republished under a topic prefixed with the publishing participant's
// sanitized identity, e.g. /ros_portal/out from participant ros-portal-test-a becomes
// /ros_portal_test_a/ros_portal/out.
TEST_F(RosPortalTestE2E, PreserveIdPrefixesInboundTopicWithPublisherIdentity) {
  // Only ROS Portal B (the A->B receiver) opts into preserve_id.
  initializeRuntime(kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic, kBidirectionalTopic,
                    /*preserve_id_a=*/false, /*preserve_id_b=*/true);

  const auto prefix = utils::sanitizeRosNameToken(identityA());
  ASSERT_TRUE(prefix.has_value());
  const std::string expected_inbound_topic = "/" + prefix.value() + kBidirectionalTopic;

  EXPECT_TRUE(verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, expected_inbound_topic,
                              "message from ROS Portal A"));
}

TEST_F(RosPortalTestE2E, DoesNotRepublishTopicNotAllowedOnReceiver) {
  constexpr const char* kSenderOnlyTopic = "/ros_portal/sender_only";
  constexpr const char* kReceiverAllowedTopic = "/ros_portal/receiver_allowed";

  initializeRuntime(kSenderOnlyTopic, kReceiverAllowedTopic, kSenderOnlyTopic, kReceiverAllowedTopic);

  EXPECT_TRUE(verifyDirectionNotForwarded(publisherA(), robotBNode(), kSenderOnlyTopic, kSenderOnlyTopic,
                                          "message that should stay blocked"));
}

// Case: A valid track published after ROS Portal startup but before a ROS
// subscriber appears should begin forwarding when the subscriber joins.
// 1. Start the receiving ROS Portal node without a ROS subscriber.
// 2. Publish a valid LiveKit data track.
// 3. Add the ROS subscriber after ROS Portal discovers the track.
// 4. Push a frame and verify the late subscriber receives it.
TEST_F(DataTrackLifecycleTests, TrackAfterRosPortalBeforeSubscriberForwards) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/ros_portal/lifecycle/track_after_ros_portal";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result) << track_result.error().message;
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
// 1. Start ROS Portal and publish a valid track.
// 2. Verify ROS Portal creates the ROS publisher.
// 3. Unpublish the LiveKit track.
// 4. Verify the ROS publisher disappears.
TEST_F(DataTrackLifecycleTests, UnpublishingTrackRemovesRosPublisher) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/ros_portal/lifecycle/unpublish";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result) << track_result.error().message;
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
  constexpr const char* kTopic = "/ros_portal/lifecycle/republish";
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

// Case: Destroying a ROS Portal node with an active inbound track should stop its reader
// and remove its ROS publisher without hanging.
// 1. Start ROS Portal and publish a valid track.
// 2. Verify the ROS publisher exists.
// 3. Destroy ROS Portal while the track remains active.
// 4. Verify teardown completes and the ROS publisher disappears.
TEST_F(DataTrackLifecycleTests, RosPortalShutdownCleansUpActiveTrack) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  constexpr const char* kTopic = "/ros_portal/lifecycle/ros_portal_shutdown";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result) << track_result.error().message;
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kTopic) > 0U; }, kGraphTimeout));

  const auto start = std::chrono::steady_clock::now();
  shutdownRosPortalB();
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
  constexpr const char* kTopic = "/ros_portal/lifecycle/subscriber_recreated";
  initializeInboundOnlyRuntime(kTopic);

  livekit::Room publisher_room;
  const auto publisher = connectPublisher(publisher_room);
  ASSERT_NE(publisher, nullptr);
  ASSERT_TRUE(defineStringSchema(*publisher));
  const auto track_result = publisher->publishDataTrack(stringTrackOptions(kTopic));
  ASSERT_TRUE(track_result) << track_result.error().message;
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
} // namespace ros_portal::test
