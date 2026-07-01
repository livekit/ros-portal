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

#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <string>
#include <vector>

#include "bridge_e2e_fixture.hpp"

namespace ros2_livekit_bridge::test {
namespace {

using SetBool = std_srvs::srv::SetBool;

constexpr const char* kServiceType = "std_srvs/srv/SetBool";

// Real SetBool server used as the ultimate target of a forwarded call:
// success mirrors the request flag and the message is recognizable per flag.
std::shared_ptr<rclcpp::Service<SetBool>> makeSetBoolServer(const std::shared_ptr<rclcpp::Node>& node,
                                                            const std::string& service_name) {
  return node->create_service<SetBool>(
      service_name, [](const SetBool::Request::SharedPtr request, SetBool::Response::SharedPtr response) {
        response->success = request->data;
        response->message = request->data ? "enabled" : "disabled";
      });
}

// Call a proxy SetBool service once via a fresh client. Returns std::nullopt
// when the service is not advertised or the call does not complete in time.
std::optional<SetBool::Response> callProxySetBoolOnce(const std::shared_ptr<rclcpp::Node>& node,
                                                      const std::string& service_name, bool data,
                                                      std::chrono::seconds call_timeout) {
  auto client = node->create_client<SetBool>(service_name);
  if (!client->wait_for_service(100ms)) {
    return std::nullopt;
  }
  auto request = std::make_shared<SetBool::Request>();
  request->data = data;
  auto future = client->async_send_request(request);
  if (future.wait_for(call_timeout) != std::future_status::ready) {
    return std::nullopt;
  }
  return *future.get();
}

// Retry the proxy call until the full round trip completes (remote participant
// visible and the remote ROS service responds) or the message timeout elapses.
// A returned value means a real response came back through the bridge; a
// dropped/failed forward yields no response, so the client never readies.
std::optional<SetBool::Response> callProxySetBool(const std::shared_ptr<rclcpp::Node>& node,
                                                  const std::string& service_name, bool data) {
  std::optional<SetBool::Response> result;
  waitFor(
      [&]() {
        result = callProxySetBoolOnce(node, service_name, data, 2s);
        return result.has_value();
      },
      kMessageTimeout);
  return result;
}

// End-to-end service forwarding: two isolated ROS graphs, each behind its own
// bridge participant in the same LiveKit room. A request issued against the
// local proxy service on one graph is serialized to CDR, base64-framed, carried
// over a LiveKit RPC to the peer bridge, replayed against the real ROS service
// on the peer graph, and the response is returned the same way in reverse.
//
// `/svc/a_to_b` is served on graph B and proxied on graph A (out on A, in on B).
// `/svc/b_to_a` is served on graph A and proxied on graph B (out on B, in on A).
TEST_F(BridgeTestE2E, ForwardsServiceCallsBothWays) {
  const std::string a_to_b = "/svc/a_to_b";
  const std::string b_to_a = "/svc/b_to_a";

  const auto config_a = bridgeServiceConfigYaml(testLiveKitRoom(), {
                                                                       {a_to_b, kServiceType, "out", identityB()},
                                                                       {b_to_a, kServiceType, "in", identityB()},
                                                                   });
  const auto config_b = bridgeServiceConfigYaml(testLiveKitRoom(), {
                                                                       {a_to_b, kServiceType, "in", identityA()},
                                                                       {b_to_a, kServiceType, "out", identityA()},
                                                                   });
  initializeBridges(config_a, config_b);

  // The real services live on the robot nodes, reached through each bridge's
  // inbound handler.
  auto server_b = makeSetBoolServer(robotBNode(), a_to_b);
  auto server_a = makeSetBoolServer(robotANode(), b_to_a);
  ASSERT_NE(server_a, nullptr);
  ASSERT_NE(server_b, nullptr);
  ASSERT_TRUE(waitFor([&]() { return serviceExists(*robotBNode(), a_to_b); }, kGraphTimeout))
      << "A->B target service did not appear in graph B";
  ASSERT_TRUE(waitFor([&]() { return serviceExists(*robotANode(), b_to_a); }, kGraphTimeout))
      << "B->A target service did not appear in graph A";

  // A -> B: robot A calls the proxy on bridge A; bridge B replays it on robot B.
  const auto a_to_b_true = callProxySetBool(robotANode(), a_to_b, true);
  ASSERT_TRUE(a_to_b_true.has_value()) << "A->B service call did not complete";
  EXPECT_TRUE(a_to_b_true->success);
  EXPECT_EQ(a_to_b_true->message, "enabled");

  const auto a_to_b_false = callProxySetBool(robotANode(), a_to_b, false);
  ASSERT_TRUE(a_to_b_false.has_value()) << "A->B service call did not complete";
  EXPECT_FALSE(a_to_b_false->success);
  EXPECT_EQ(a_to_b_false->message, "disabled");

  // B -> A: robot B calls the proxy on bridge B; bridge A replays it on robot A.
  const auto b_to_a_true = callProxySetBool(robotBNode(), b_to_a, true);
  ASSERT_TRUE(b_to_a_true.has_value()) << "B->A service call did not complete";
  EXPECT_TRUE(b_to_a_true->success);
  EXPECT_EQ(b_to_a_true->message, "enabled");
}

// The outbound proxy is advertised eagerly, but when the remote ROS service is
// absent the inbound handler has nothing to call, so no response is ever
// returned and the local ROS caller times out (a ROS service has no in-band
// error channel).
TEST_F(BridgeTestE2E, OutboundCallTimesOutWhenRemoteServiceMissing) {
  const std::string missing = "/svc/missing";

  const auto config_a = bridgeServiceConfigYaml(testLiveKitRoom(), {{missing, kServiceType, "out", identityB()}});
  const auto config_b = bridgeServiceConfigYaml(testLiveKitRoom(), {{missing, kServiceType, "in", identityA()}});
  initializeBridges(config_a, config_b);

  // The proxy server itself must exist (created at bridge construction).
  auto client = robotANode()->create_client<SetBool>(missing);
  ASSERT_TRUE(waitFor([&]() { return client->wait_for_service(100ms); }, kGraphTimeout))
      << "Bridge A did not advertise the outbound proxy service";

  // No server is created on robot B, so the round trip can never complete.
  bool got_response = false;
  const auto deadline = std::chrono::steady_clock::now() + kNegativeAssertionTimeout;
  while (std::chrono::steady_clock::now() < deadline && !got_response) {
    auto request = std::make_shared<SetBool::Request>();
    request->data = true;
    auto future = client->async_send_request(request);
    if (future.wait_for(1s) == std::future_status::ready) {
      got_response = true;
    }
  }
  EXPECT_FALSE(got_response) << "Expected no response when the remote ROS service is missing";
}

} // namespace
} // namespace ros2_livekit_bridge::test
