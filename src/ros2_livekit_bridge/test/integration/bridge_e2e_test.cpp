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

} // namespace
} // namespace ros2_livekit_bridge::test
