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

#include "ros2_livekit_bridge/latched_topic_forwarder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/cli/json_converters.hpp"
#include "ros2_livekit_bridge/types.hpp"
#include "ros2_livekit_bridge/utils/base64.hpp"

// The fixture lives in the named namespace (not an anonymous one) so the
// FRIEND_TEST declarations in the header match the generated test classes and
// can reach the forwarder's private store / push internals.
namespace ros2_livekit_bridge {

class LatchedTopicForwarderTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<rclcpp::Node>("latched_topic_forwarder_unit_test"); }
  void TearDown() override { node_.reset(); }

  LatchedTopicForwarder::Options makeOptions() {
    LatchedTopicForwarder::Options options;
    options.outbound_topics = {"/tf_static"};
    options.max_participant_failures = 3;
    // Large interval so the push worker never fires on its own during a test;
    // tests drive pushToPeers() directly for determinism.
    options.push_interval = std::chrono::hours(1);
    return options;
  }

  LatchedTopicForwarder::LiveKitMethods makeMethods() {
    LatchedTopicForwarder::LiveKitMethods methods;
    methods.register_rpc_method = [this](const std::string& method, RpcHandler handler) {
      registered_method_ = method;
      registered_handler_ = std::move(handler);
      return true;
    };
    methods.unregister_rpc_method = [this](const std::string& method) {
      unregistered_method_ = method;
      return true;
    };
    methods.perform_rpc = [this](const std::string& id, const std::string&, const std::string& payload,
                                 std::uint8_t) -> std::optional<std::string> {
      rpc_calls_.push_back({id, payload});
      if (rpc_should_succeed_) {
        return cliResponseToJson(true, "", "");
      }
      return std::nullopt;
    };
    methods.list_remote_identities = [this]() { return roster_; };
    return methods;
  }

  std::shared_ptr<rclcpp::Node> node_;

  // Stub state observed/controlled by tests.
  std::vector<std::string> roster_;
  std::vector<std::pair<std::string, std::string>> rpc_calls_;
  bool rpc_should_succeed_ = true;
  std::string registered_method_;
  std::string unregistered_method_;
  RpcHandler registered_handler_;
};

TEST_F(LatchedTopicForwarderTest, ConstructorRejectsExpiredNode) {
  EXPECT_THROW(LatchedTopicForwarder(makeOptions(), rclcpp::Node::WeakPtr{}, makeMethods()), std::invalid_argument);
}

TEST_F(LatchedTopicForwarderTest, ConstructorRejectsMissingMethods) {
  EXPECT_THROW(LatchedTopicForwarder(makeOptions(), node_, LatchedTopicForwarder::LiveKitMethods{}),
               std::invalid_argument);
}

TEST_F(LatchedTopicForwarderTest, StoresDistinctMessagesAndBumpsVersion) {
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods());
  const std::vector<std::uint8_t> a = {1, 2, 3};
  const std::vector<std::uint8_t> b = {4, 5, 6};

  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());
  EXPECT_EQ(forwarder.messages_.size(), 1u);
  EXPECT_EQ(forwarder.version_, 1u);

  // Duplicate content: not stored, no version bump.
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());
  EXPECT_EQ(forwarder.messages_.size(), 1u);
  EXPECT_EQ(forwarder.version_, 1u);

  // Distinct content: stored, version bumps.
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", b.data(), b.size());
  EXPECT_EQ(forwarder.messages_.size(), 2u);
  EXPECT_EQ(forwarder.version_, 2u);
}

TEST_F(LatchedTopicForwarderTest, SkipsOversizeMessage) {
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods());
  // Already over 15 KiB before base64 inflation.
  const std::vector<std::uint8_t> big(20 * 1024, 0xAB);
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", big.data(), big.size());
  EXPECT_TRUE(forwarder.messages_.empty());
  EXPECT_EQ(forwarder.version_, 0u);
}

TEST_F(LatchedTopicForwarderTest, PushesStoredStateToPeers) {
  roster_ = {"peerA"};
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods());
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  forwarder.pushToPeers();
  ASSERT_EQ(rpc_calls_.size(), 1u);
  EXPECT_EQ(rpc_calls_[0].first, "peerA");
  ASSERT_EQ(forwarder.participant_states_.count("peerA"), 1u);
  EXPECT_EQ(forwarder.participant_states_["peerA"].delivered_version, forwarder.version_);

  // Already delivered at the current version: a second cycle sends nothing.
  forwarder.pushToPeers();
  EXPECT_EQ(rpc_calls_.size(), 1u);
}

TEST_F(LatchedTopicForwarderTest, GivesUpAfterFailureCapUntilNewVersion) {
  roster_ = {"peerA"};
  rpc_should_succeed_ = false;
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods()); // max_participant_failures = 3
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  for (int i = 0; i < 3; ++i) {
    forwarder.pushToPeers();
  }
  EXPECT_EQ(rpc_calls_.size(), 3u);
  EXPECT_EQ(forwarder.participant_states_["peerA"].consecutive_failures, 3u);

  // Given up: further cycles do not attempt the peer.
  forwarder.pushToPeers();
  EXPECT_EQ(rpc_calls_.size(), 3u);

  // New state resets failures and re-arms the peer.
  rpc_should_succeed_ = true;
  const std::vector<std::uint8_t> b = {4, 5, 6};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", b.data(), b.size());
  forwarder.pushToPeers();
  EXPECT_GT(rpc_calls_.size(), 3u);
  EXPECT_EQ(forwarder.participant_states_["peerA"].delivered_version, forwarder.version_);
}

TEST_F(LatchedTopicForwarderTest, ForgetsParticipantThatLeaves) {
  roster_ = {"peerA"};
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods());
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  forwarder.pushToPeers();
  EXPECT_EQ(forwarder.participant_states_.count("peerA"), 1u);

  // Peer leaves: its state is dropped so a rejoin re-pushes from scratch.
  roster_.clear();
  forwarder.pushToPeers();
  EXPECT_EQ(forwarder.participant_states_.count("peerA"), 0u);
}

TEST_F(LatchedTopicForwarderTest, InboundHandlerValidatesTopic) {
  auto options = makeOptions();
  options.outbound_topics.clear();
  options.inbound_topics = {"/tf_static"};
  LatchedTopicForwarder forwarder(std::move(options), node_, makeMethods());

  // Registered the inbound handler because an inbound latched topic exists.
  EXPECT_EQ(registered_method_, kLatchedStateRpcMethod);

  // Unknown topic -> failure envelope.
  nlohmann::json request;
  request["topic"] = "/not_configured";
  request["msg_type"] = "std_msgs/msg/String";
  request["data"] = utils::base64Encode(std::vector<std::uint8_t>{1, 2, 3});
  const auto rejected = nlohmann::json::parse(forwarder.handleLatchedStateRpc(request.dump()));
  EXPECT_FALSE(rejected.at("success").get<bool>());

  // Malformed request -> failure envelope.
  const auto malformed = nlohmann::json::parse(forwarder.handleLatchedStateRpc("not json"));
  EXPECT_FALSE(malformed.at("success").get<bool>());
}

} // namespace ros2_livekit_bridge
