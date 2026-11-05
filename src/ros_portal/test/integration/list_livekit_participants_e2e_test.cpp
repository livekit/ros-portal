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
#include <livekit/room.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "ros_portal_e2e_fixture.hpp"
#include "ros_portal_msgs/srv/list_livekit_participants.hpp"

namespace ros_portal::test {
namespace {

using ParticipantsSrv = ros_portal_msgs::srv::ListLivekitParticipants;

TEST_F(RosPortalTestE2E, ListsLocalAndNonPortalParticipantsAcrossPeerJoinAndLeave) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  initializeInboundOnlyRuntime("/participant_listing");
  auto client = robotBNode()->create_client<ParticipantsSrv>(
      rosPortalServiceName(robotBNode(), "ros_portal/list_livekit_participants"));
  ASSERT_TRUE(client->wait_for_service(kGraphTimeout));

  ParticipantsSrv::Response::SharedPtr response;
  const auto lists = [&](const std::vector<std::string>& expected) {
    auto future = client->async_send_request(std::make_shared<ParticipantsSrv::Request>());
    if (future.wait_for(1s) != std::future_status::ready) {
      client->remove_pending_request(future);
      return false;
    }
    response = future.get();
    return response->success && response->err_msg.empty() && response->participant_identities == expected;
  };

  // A connected room containing only the portal is a successful, nonempty result.
  ASSERT_TRUE(waitFor([&]() { return lists({identityB()}); }, kGraphTimeout));

  // This raw SDK peer has no ROS Portal or robot attribute; it must still be listed.
  livekit::Room peer;
  ASSERT_TRUE(peer.connect(liveKitUrl(), tokenA(), livekit::RoomOptions{}));
  std::vector<std::string> expected{identityA(), identityB()};
  std::sort(expected.begin(), expected.end());
  ASSERT_TRUE(waitFor([&]() { return lists(expected); }, kGraphTimeout));
  EXPECT_EQ(std::count(response->participant_identities.begin(), response->participant_identities.end(), identityA()),
            1);

  ASSERT_TRUE(peer.disconnect());
  ASSERT_TRUE(waitFor([&]() { return lists({identityB()}); }, kGraphTimeout));

  ASSERT_TRUE(peer.connect(liveKitUrl(), tokenA(), livekit::RoomOptions{}));
  ASSERT_TRUE(waitFor([&]() { return lists(expected); }, kGraphTimeout));
}

} // namespace
} // namespace ros_portal::test
