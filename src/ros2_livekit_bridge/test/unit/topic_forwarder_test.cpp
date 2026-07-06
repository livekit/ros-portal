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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
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

namespace {

// Options with a single outbound data topic and an optional rate cap on it.
TopicForwarder::TopicForwarderOptions makeRateCapOptions(std::optional<double> max_rate_hz) {
  TopicForwarder::TopicForwarderOptions options;
  options.outgoing_topic_patterns = utils::compileRegexPatterns(std::vector<std::string>{"/allowed/.*"});
  options.min_qos_depth = 2;
  options.max_qos_depth = 10;
  if (max_rate_hz.has_value()) {
    options.outbound_rate_limits = {{"/allowed/data", *max_rate_hz}};
  }
  return options;
}

// LiveKit callbacks whose data-track writer counts every forwarded payload.
TopicForwarder::LiveKitMethods makeCountingLiveKitMethods(std::shared_ptr<std::atomic<int>> push_count) {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track =
      [push_count](
          const std::string &) -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
    writer->try_push = [push_count](std::vector<std::uint8_t>) {
      push_count->fetch_add(1);
      return livekit::Result<void, std::string>::success();
    };
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
  };
  livekit_methods.publish_video_track =
      [](const std::string &, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  return livekit_methods;
}

TopicForwarder::LiveKitMethods makeFlakyLiveKitMethods(std::shared_ptr<std::atomic<int>> push_count,
                                                       std::shared_ptr<std::atomic<int>> remaining_failures) {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track =
      [push_count, remaining_failures](
          const std::string &) -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
    writer->try_push = [push_count, remaining_failures](std::vector<std::uint8_t>) {
      if (remaining_failures->fetch_sub(1) > 0) {
        return livekit::Result<void, std::string>::failure("backpressure");
      }
      push_count->fetch_add(1);
      return livekit::Result<void, std::string>::success();
    };
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
  };
  livekit_methods.publish_video_track =
      [](const std::string &, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  return livekit_methods;
}

// LiveKit callbacks whose data-track writer records the most recent forwarded
// payload (in addition to counting pushes), so tests can assert which cached
// sample the rate timer emitted.
TopicForwarder::LiveKitMethods makeRecordingLiveKitMethods(std::shared_ptr<std::atomic<int>> push_count,
                                                           std::shared_ptr<std::vector<std::uint8_t>> last_payload) {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track =
      [push_count, last_payload](
          const std::string &) -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
    writer->try_push = [push_count, last_payload](std::vector<std::uint8_t> payload) {
      *last_payload = std::move(payload);
      push_count->fetch_add(1);
      return livekit::Result<void, std::string>::success();
    };
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
  };
  livekit_methods.publish_video_track =
      [](const std::string &, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  return livekit_methods;
}

} // namespace

// A rate-capped topic forwards on a wall timer at max_rate_hz, not on arrival.
// A dense burst of input samples is therefore downsampled to the handful of
// timer ticks that elapse in the window, far fewer than the number published.
TEST_F(TopicForwarderTest, RateCapDownsamplesBurstToTimerRate) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  // 20 Hz cap -> one tick every 50 ms.
  TopicForwarder forwarder(makeRateCapOptions(20.0), node_, makeCountingLiveKitMethods(push_count));

  rclcpp::QoS pub_qos{rclcpp::KeepLast(50)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String msg;
  msg.data = "x";
  constexpr int kSamples = 30;
  for (int i = 0; i < kSamples; ++i) {
    publisher->publish(msg);
  }

  // Wait for the first tick, then spin ~175 ms (a few more ticks). Only a few
  // samples are forwarded, well below the 30 published.
  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }));
  spinUntil([]() { return false; }, 175ms);
  EXPECT_GE(push_count->load(), 1);
  EXPECT_LE(push_count->load(), 8);
  EXPECT_LT(push_count->load(), kSamples);
}

// The timer forwards the most recently cached sample (zero-order hold): when
// several samples arrive within one period, only the newest is emitted. A slow
// cap keeps the first tick far enough out that all three inbound samples are
// processed before it fires.
TEST_F(TopicForwarderTest, RateCapForwardsMostRecentSample) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  auto last_payload = std::make_shared<std::vector<std::uint8_t>>();
  // 5 Hz cap -> 200 ms period, ample time to drain the three sends first.
  TopicForwarder forwarder(makeRateCapOptions(5.0), node_, makeRecordingLiveKitMethods(push_count, last_payload));

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  for (const auto *text : {"first", "second", "third"}) {
    std_msgs::msg::String msg;
    msg.data = text;
    publisher->publish(msg);
  }

  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }));

  // The forwarded payload must be the CDR encoding of the newest sample.
  std_msgs::msg::String latest;
  latest.data = "third";
  rclcpp::Serialization<std_msgs::msg::String> serializer;
  rclcpp::SerializedMessage expected;
  serializer.serialize_message(&latest, &expected);
  const auto &expected_rcl = expected.get_rcl_serialized_message();
  const std::vector<std::uint8_t> expected_bytes(expected_rcl.buffer, expected_rcl.buffer + expected_rcl.buffer_length);

  EXPECT_EQ(*last_payload, expected_bytes);
}

// The dirty flag keeps the cap from becoming a rate floor: after a single
// sample, subsequent ticks with no new input forward nothing.
TEST_F(TopicForwarderTest, RateCapDoesNotResendWhenIdle) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  // 50 Hz -> 20 ms period, so ~10 ticks elapse during the wait window.
  TopicForwarder forwarder(makeRateCapOptions(50.0), node_, makeCountingLiveKitMethods(push_count));

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String msg;
  msg.data = "x";
  publisher->publish(msg);

  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }));
  spinUntil([]() { return false; }, 200ms);
  EXPECT_EQ(push_count->load(), 1);
}

// A failed push does not consume the cached sample: it is retained and re-armed,
// so a later tick retries and forwards it exactly once.
TEST_F(TopicForwarderTest, RateCapRetriesAfterFailedPush) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  auto remaining_failures = std::make_shared<std::atomic<int>>(1);
  // 50 Hz -> 20 ms period so several ticks occur within the window.
  TopicForwarder forwarder(makeRateCapOptions(50.0), node_, makeFlakyLiveKitMethods(push_count, remaining_failures));

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String msg;
  msg.data = "x";
  publisher->publish(msg);

  // First tick's push fails; a later tick retries and succeeds exactly once.
  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }, 500ms));
  spinUntil([]() { return false; }, 150ms);
  EXPECT_EQ(push_count->load(), 1);
}

// Without a configured cap, every delivered sample is forwarded. Publishing one
// at a time and spinning between sends keeps the samples from being coalesced by
// the reader's KEEP_LAST queue, isolating the "no throttling" behaviour under test.
TEST_F(TopicForwarderTest, UncappedTopicForwardsEverySample) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  TopicForwarder forwarder(makeRateCapOptions(std::nullopt), node_, makeCountingLiveKitMethods(push_count));

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String msg;
  msg.data = "x";
  constexpr int kSamples = 4;
  for (int i = 0; i < kSamples; ++i) {
    publisher->publish(msg);
    ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= i + 1; }));
  }

  EXPECT_EQ(push_count->load(), kSamples);
}

} // namespace ros2_livekit_bridge
