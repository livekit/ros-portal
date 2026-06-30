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

#include "ros2_livekit_bridge/topic_forwarder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

// TopicForwarder now creates its subscriptions and publishers directly on the
// ROS node it is given, so these unit tests cover only the node-independent
// logic (construction validation, incoming-topic matching, and QoS
// negotiation against the live ROS graph). End-to-end forwarding behaviour
// (data push, image capture, inbound republishing) is exercised by the
// integration tests.
namespace ros2_livekit_bridge {
namespace {

TopicForwarder::TopicForwarderOptions makeOptions() {
  TopicForwarder::TopicForwarderOptions options;
  options.outgoing_topic_patterns = utils::compileRegexPatterns(std::vector<std::string>{"/allowed/.*"});
  options.incoming_topic_patterns = utils::compileRegexPatterns(std::vector<std::string>{"/remote/.*"});
  options.best_effort_qos_topic_patterns = utils::compileRegexPatterns(std::vector<std::string>{"/best_effort"});
  options.min_qos_depth = 2;
  options.max_qos_depth = 4;
  return options;
}

// Minimal LiveKit callbacks that only need to be present for the forwarder to
// construct; these tests never publish to a LiveKit track.
TopicForwarder::LiveKitMethods makeLiveKitMethods() {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track =
      [](const std::string &) -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure("unused");
  };
  livekit_methods.publish_video_track =
      [](const std::string &, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  return livekit_methods;
}

} // namespace

using namespace std::chrono_literals;

class TopicForwarderTest : public ::testing::Test {
protected:
  void SetUp() override { node_ = std::make_shared<rclcpp::Node>("topic_forwarder_unit_test"); }

  void TearDown() override { node_.reset(); }

  TopicForwarder makeForwarder() { return TopicForwarder(makeOptions(), node_, makeLiveKitMethods()); }

  // Spins the node until the ROS graph reflects a predicate (e.g. a freshly
  // created publisher has been discovered) or the timeout elapses.
  bool spinUntil(const std::function<bool()> &predicate, std::chrono::milliseconds timeout = 2s) {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    executor.spin_some();
    return predicate();
  }

  bool waitForPublishers(const std::string &topic, size_t expected) {
    return spinUntil([&]() { return node_->get_publishers_info_by_topic(topic).size() == expected; });
  }

  std::shared_ptr<rclcpp::Node> node_;
};

TEST_F(TopicForwarderTest, ConstructorRejectsExpiredNode) {
  EXPECT_THROW(TopicForwarder(makeOptions(), rclcpp::Node::WeakPtr{}, makeLiveKitMethods()), std::invalid_argument);
}

TEST_F(TopicForwarderTest, ConstructorRejectsMissingLiveKitMethods) {
  EXPECT_THROW(TopicForwarder(makeOptions(), node_, TopicForwarder::LiveKitMethods{}), std::invalid_argument);
}

TEST_F(TopicForwarderTest, IncomingTopicAllowedUsesConfiguredPatterns) {
  auto forwarder = makeForwarder();

  EXPECT_TRUE(forwarder.isIncomingTopicAllowed("/remote/cmd"));
  EXPECT_FALSE(forwarder.isIncomingTopicAllowed("/blocked/cmd"));
}

TEST_F(TopicForwarderTest, QoSDefaultsToMinDepthBestEffortVolatile) {
  auto forwarder = makeForwarder();

  const auto qos = forwarder.determineQoS("/allowed/data");

  EXPECT_EQ(qos.depth(), 2u);
  EXPECT_EQ(qos.reliability(), rclcpp::ReliabilityPolicy::BestEffort);
  EXPECT_EQ(qos.durability(), rclcpp::DurabilityPolicy::Volatile);
}

// History depth is not propagated through DDS discovery, so endpoint info
// reports depth 0 and the depth-summing/clamping branch of determineQoS cannot
// be observed here; these tests assert only the reliability/durability
// negotiation, which discovery does report.
TEST_F(TopicForwarderTest, QoSUsesReliableTransientLocalWhenAllPublishersMatch) {
  rclcpp::QoS offered_qos{rclcpp::KeepLast(3)};
  offered_qos.reliable();
  offered_qos.transient_local();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", offered_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  auto forwarder = makeForwarder();
  const auto qos = forwarder.determineQoS("/allowed/data");

  EXPECT_EQ(qos.reliability(), rclcpp::ReliabilityPolicy::Reliable);
  EXPECT_EQ(qos.durability(), rclcpp::DurabilityPolicy::TransientLocal);
}

TEST_F(TopicForwarderTest, QoSFallsBackForMixedPolicies) {
  rclcpp::QoS reliable_qos{rclcpp::KeepLast(5)};
  reliable_qos.reliable();
  reliable_qos.transient_local();
  rclcpp::QoS best_effort_qos{rclcpp::KeepLast(5)};
  best_effort_qos.best_effort();
  best_effort_qos.durability_volatile();
  auto reliable_publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", reliable_qos);
  auto best_effort_publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", best_effort_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 2u));

  auto forwarder = makeForwarder();
  const auto qos = forwarder.determineQoS("/allowed/data");

  EXPECT_EQ(qos.reliability(), rclcpp::ReliabilityPolicy::BestEffort);
  EXPECT_EQ(qos.durability(), rclcpp::DurabilityPolicy::Volatile);
}

TEST_F(TopicForwarderTest, QoSBestEffortOverrideWins) {
  rclcpp::QoS reliable_qos{rclcpp::KeepLast(3)};
  reliable_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/best_effort", reliable_qos);
  ASSERT_TRUE(waitForPublishers("/best_effort", 1u));

  auto forwarder = makeForwarder();
  const auto qos = forwarder.determineQoS("/best_effort");

  EXPECT_EQ(qos.reliability(), rclcpp::ReliabilityPolicy::BestEffort);
}

} // namespace ros2_livekit_bridge
