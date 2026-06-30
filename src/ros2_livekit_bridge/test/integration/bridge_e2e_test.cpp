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

#include "bridge_e2e_fixture.hpp"

namespace ros2_livekit_bridge::test {
namespace {

// End-to-end bridge check: two isolated ROS graphs publish message topics through
// separate bridge participants in the same LiveKit room, then verify each graph
// receives the other's message only via the bridge. This catches regressions in
// LiveKit data-track forwarding, participant topic prefixing, and ROS graph
// isolation without relying on shared local ROS discovery.
TEST_F(BridgeTestE2E, RepublishesRosMessagesBothWays) {
  initializeRuntime(kBidirectionalTopic);
  const auto expected_topic_from_a = expectedInboundTopicName(identityA(), kBidirectionalTopic);
  const auto expected_topic_from_b = expectedInboundTopicName(identityB(), kBidirectionalTopic);
  ASSERT_TRUE(expected_topic_from_a.has_value());
  ASSERT_TRUE(expected_topic_from_b.has_value());

  EXPECT_TRUE(verifyDirection(publisherA(), robotBNode(), kBidirectionalTopic, *expected_topic_from_a,
                              "message from bridge a"));
  EXPECT_TRUE(verifyDirection(publisherB(), robotANode(), kBidirectionalTopic, *expected_topic_from_b,
                              "message from bridge b"));
}

TEST_F(BridgeTestE2E, DoesNotRepublishTopicNotAllowedOnReceiver) {
  constexpr const char* kSenderOnlyTopic = "/bridge/sender_only";
  constexpr const char* kReceiverAllowedTopic = "/bridge/receiver_allowed";

  initializeRuntime(kSenderOnlyTopic, kReceiverAllowedTopic, kSenderOnlyTopic, kReceiverAllowedTopic);
  const auto forbidden_topic = expectedInboundTopicName(identityA(), kSenderOnlyTopic);
  ASSERT_TRUE(forbidden_topic.has_value());

  EXPECT_TRUE(verifyDirectionNotForwarded(publisherA(), robotBNode(), kSenderOnlyTopic, *forbidden_topic,
                                          "message that should stay blocked"));
}

} // namespace
} // namespace ros2_livekit_bridge::test
