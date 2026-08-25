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

#include "ros_portal/latched_topic_forwarder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostics_test_utils.hpp"
#include "ros_portal/cli/json_converters.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/types.hpp"
#include "ros_portal/utils/base64.hpp"

// The fixture lives in the named namespace (not an anonymous one) so the
// FRIEND_TEST declarations in the header match the generated test classes and
// can reach the forwarder's private store / push internals.
namespace ros_portal {

namespace {

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

} // namespace

class LatchedTopicForwarderTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<rclcpp::Node>("latched_topic_forwarder_unit_test");
    diagnostics_updater_ = std::make_shared<diagnostic_updater::Updater>(node_);
    diagnostics_updater_->setHardwareID("ros_portal");
    diagnostics_fns_ = test::makeDiagnosticsFns(diagnostics_updater_);
  }

  void TearDown() override {
    diagnostics_fns_ = {};
    diagnostics_updater_.reset();
    node_.reset();
  }

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
    methods.is_room_available = [this]() { return room_available_; };
    methods.register_rpc_method = [this](const std::string& method, RpcHandler handler) {
      registered_method_ = method;
      registered_handler_ = std::move(handler);
      return rpc_registration_succeeds_;
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
  std::shared_ptr<diagnostic_updater::Updater> diagnostics_updater_;
  diagnostics::DiagnosticsManagerFns diagnostics_fns_;

  // Stub state observed/controlled by tests.
  std::vector<std::string> roster_;
  std::vector<std::pair<std::string, std::string>> rpc_calls_;
  bool room_available_ = true;
  bool rpc_should_succeed_ = true;
  bool rpc_registration_succeeds_ = true;
  std::string registered_method_;
  std::string unregistered_method_;
  RpcHandler registered_handler_;
};

TEST_F(LatchedTopicForwarderTest, ConstructorRejectsExpiredNode) {
  EXPECT_THROW(LatchedTopicForwarder(makeOptions(), rclcpp::Node::WeakPtr{}, makeMethods(), diagnostics_fns_),
               std::invalid_argument);
}

TEST_F(LatchedTopicForwarderTest, ConstructorRejectsMissingMethods) {
  EXPECT_THROW(LatchedTopicForwarder(makeOptions(), node_, LatchedTopicForwarder::LiveKitMethods{}, diagnostics_fns_),
               std::invalid_argument);
}

TEST_F(LatchedTopicForwarderTest, ConstructorRejectsMissingDiagnostics) {
  EXPECT_THROW(LatchedTopicForwarder(makeOptions(), node_, makeMethods(), {}), std::invalid_argument);
}

TEST_F(LatchedTopicForwarderTest, DiagnosticsReportConfigurationAndRpcRegistration) {
  auto options = makeOptions();
  options.inbound_topics = {"/tf_static"};
  LatchedTopicForwarder forwarder(std::move(options), node_, makeMethods(), diagnostics_fns_);

  diagnostic_updater::DiagnosticStatusWrapper status;
  forwarder.populateStatus(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(status, "rpc_registered"), "true");
  EXPECT_EQ(valueFor(status, "outbound.failures"), "0");
  EXPECT_EQ(valueFor(status, "peers.total"), "0");
  EXPECT_EQ(valueFor(status, "peers.behind"), "0");
  EXPECT_EQ(valueFor(status, "peers.given_up"), "0");
  EXPECT_EQ(valueFor(status, "inbound.failures"), "0");

  // Configured and per-item inventory is logged rather than published as fields; the
  // undiscovered outbound topic still drives the WARN summary asserted above.
  EXPECT_FALSE(valueFor(status, "outbound.topics_configured").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.topics_subscribed").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.messages_stored").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.max_stored_messages").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.state_version").has_value());
  EXPECT_FALSE(valueFor(status, "peers.up_to_date").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.oversize_drops").has_value());
  EXPECT_FALSE(valueFor(status, "outbound.push_failures").has_value());
  EXPECT_FALSE(valueFor(status, "time_since_last_successful_push_sec").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.topics_configured").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.publishers_created").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.rpc_requests").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.rejected_unconfigured_topic").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.malformed_payloads").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.base64_decode_failures").has_value());
  EXPECT_FALSE(valueFor(status, "inbound.publish_failures").has_value());
}

TEST_F(LatchedTopicForwarderTest, DiagnosticsErrorWhenRpcRegistrationFails) {
  auto options = makeOptions();
  options.outbound_topics.clear();
  options.inbound_topics = {"/tf_static"};
  rpc_registration_succeeds_ = false;
  LatchedTopicForwarder forwarder(std::move(options), node_, makeMethods(), diagnostics_fns_);

  diagnostic_updater::DiagnosticStatusWrapper status;
  forwarder.populateStatus(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(valueFor(status, "rpc_registered"), "false");
}

TEST_F(LatchedTopicForwarderTest, StoresDistinctMessagesAndBumpsVersion) {
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(), diagnostics_fns_);
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
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(), diagnostics_fns_);
  // Already over 15 KiB before base64 inflation.
  const std::vector<std::uint8_t> big(20 * 1024, 0xAB);
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", big.data(), big.size());
  EXPECT_TRUE(forwarder.messages_.empty());
  EXPECT_EQ(forwarder.version_, 0u);

  diagnostic_updater::DiagnosticStatusWrapper status;
  forwarder.populateStatus(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(status, "outbound.failures"), "1");
}

TEST_F(LatchedTopicForwarderTest, PushesStoredStateToPeers) {
  roster_ = {"peerA"};
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(), diagnostics_fns_);
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  forwarder.pushToPeers();
  ASSERT_EQ(rpc_calls_.size(), 1u);
  EXPECT_EQ(rpc_calls_[0].first, "peerA");
  ASSERT_EQ(forwarder.participant_states_.count("peerA"), 1u);
  EXPECT_EQ(forwarder.participant_states_["peerA"].delivered_version, forwarder.version_);

  diagnostic_updater::DiagnosticStatusWrapper status;
  forwarder.populateStatus(status);
  EXPECT_EQ(valueFor(status, "peers.total"), "1");
  EXPECT_EQ(valueFor(status, "peers.behind"), "0");
  EXPECT_EQ(valueFor(status, "peers.given_up"), "0");
  EXPECT_EQ(valueFor(status, "outbound.failures"), "0");

  // Already delivered at the current version: a second cycle sends nothing.
  forwarder.pushToPeers();
  EXPECT_EQ(rpc_calls_.size(), 1u);
}

TEST_F(LatchedTopicForwarderTest, GivesUpAfterFailureCapUntilNewVersion) {
  roster_ = {"peerA"};
  rpc_should_succeed_ = false;
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(),
                                  diagnostics_fns_); // max_participant_failures = 3
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  for (int i = 0; i < 3; ++i) {
    forwarder.pushToPeers();
  }
  EXPECT_EQ(rpc_calls_.size(), 3u);
  EXPECT_EQ(forwarder.participant_states_["peerA"].consecutive_failures, 3u);

  diagnostic_updater::DiagnosticStatusWrapper failed_status;
  forwarder.populateStatus(failed_status);
  EXPECT_EQ(failed_status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(valueFor(failed_status, "peers.total"), "1");
  EXPECT_EQ(valueFor(failed_status, "peers.behind"), "0");
  EXPECT_EQ(valueFor(failed_status, "peers.given_up"), "1");
  EXPECT_EQ(valueFor(failed_status, "outbound.failures"), "3");

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
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(), diagnostics_fns_);
  const std::vector<std::uint8_t> a = {1, 2, 3};
  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());

  forwarder.pushToPeers();
  EXPECT_EQ(forwarder.participant_states_.count("peerA"), 1u);

  // Peer leaves: its state is dropped so a rejoin re-pushes from scratch.
  roster_.clear();
  forwarder.pushToPeers();
  EXPECT_EQ(forwarder.participant_states_.count("peerA"), 0u);
}

TEST_F(LatchedTopicForwarderTest, IdlesQuietlyWhileRoomUnavailable) {
  room_available_ = false;
  roster_ = {"peerA"};
  LatchedTopicForwarder forwarder(makeOptions(), node_, makeMethods(), diagnostics_fns_);
  const std::vector<std::uint8_t> a = {1, 2, 3};

  forwarder.storeOutboundMessage("/tf_static", "tf2_msgs/msg/TFMessage", a.data(), a.size());
  EXPECT_TRUE(forwarder.messages_.empty());
  EXPECT_EQ(forwarder.version_, 0u);

  forwarder.pushToPeers();
  EXPECT_TRUE(rpc_calls_.empty());
  EXPECT_TRUE(forwarder.participant_states_.empty());
}

TEST_F(LatchedTopicForwarderTest, InboundHandlerValidatesTopic) {
  auto options = makeOptions();
  options.outbound_topics.clear();
  options.inbound_topics = {"/tf_static"};
  LatchedTopicForwarder forwarder(std::move(options), node_, makeMethods(), diagnostics_fns_);

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

  request["topic"] = "/tf_static";
  request["data"] = "not-base64!";
  const auto invalid_base64 = nlohmann::json::parse(forwarder.handleLatchedStateRpc(request.dump()));
  EXPECT_FALSE(invalid_base64.at("success").get<bool>());

  diagnostic_updater::DiagnosticStatusWrapper status;
  forwarder.populateStatus(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  // One rejection each for: unconfigured topic, malformed payload, invalid base64.
  EXPECT_EQ(valueFor(status, "inbound.failures"), "3");
}

} // namespace ros_portal
