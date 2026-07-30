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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <functional>
#include <memory>
#include <optional>
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

#include "diagnostics_test_utils.hpp"
#include "ros2_livekit_bridge/diagnostics/diagnostics_fns.hpp"
#include "ros2_livekit_bridge/schema/manager.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

// TopicForwarder now creates its subscriptions and publishers directly on the
// ROS node it is given, so these unit tests cover only the node-independent
// logic (construction validation, incoming-topic matching, and QoS
// negotiation against the live ROS graph). End-to-end forwarding behaviour
// (data push, image capture, inbound republishing) is exercised by the
// integration tests.
namespace ros2_livekit_bridge {
namespace {

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

TopicForwarder::Options makeOptions() {
  TopicForwarder::Options options;
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
  livekit_methods.publish_data_track = [](const std::string&, const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure("unused");
  };
  livekit_methods.publish_video_track =
      [](const std::string&, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  livekit_methods.schema.define_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return true; };
  livekit_methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::nullopt;
  };
  return livekit_methods;
}

} // namespace

using namespace std::chrono_literals;

class TopicForwarderTest : public ::testing::Test {
protected:
  void SetUp() override {
    node_ = std::make_shared<rclcpp::Node>("topic_forwarder_unit_test");
    diagnostics_updater_ = std::make_shared<diagnostic_updater::Updater>(node_);
    diagnostics_updater_->setHardwareID("ros2_livekit_bridge");
    diagnostics_fns_ = test::makeDiagnosticsFns(diagnostics_updater_);
  }

  void TearDown() override {
    diagnostics_fns_ = {};
    diagnostics_updater_.reset();
    node_.reset();
  }

  TopicForwarder makeForwarder() {
    return TopicForwarder(makeOptions(), node_, makeLiveKitMethods(), diagnostics_fns_);
  }

  // Spins the node until the ROS graph reflects a predicate (e.g. a freshly
  // created publisher has been discovered) or the timeout elapses.
  bool spinUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 2s) {
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

  bool waitForPublishers(const std::string& topic, size_t expected) {
    return spinUntil([&]() { return node_->get_publishers_info_by_topic(topic).size() == expected; });
  }

  std::shared_ptr<rclcpp::Node> node_;
  std::shared_ptr<diagnostic_updater::Updater> diagnostics_updater_;
  diagnostics::DiagnosticsManagerFns diagnostics_fns_;
};

TEST_F(TopicForwarderTest, ConstructorRejectsExpiredNode) {
  EXPECT_THROW(TopicForwarder(makeOptions(), rclcpp::Node::WeakPtr{}, makeLiveKitMethods(), diagnostics_fns_),
               std::invalid_argument);
}

TEST_F(TopicForwarderTest, ConstructorRejectsMissingLiveKitMethods) {
  EXPECT_THROW(TopicForwarder(makeOptions(), node_, TopicForwarder::LiveKitMethods{}, diagnostics_fns_),
               std::invalid_argument);
}

TEST_F(TopicForwarderTest, ConstructorRejectsMissingDiagnostics) {
  EXPECT_THROW(TopicForwarder(makeOptions(), node_, makeLiveKitMethods(), {}), std::invalid_argument);
}

TEST_F(TopicForwarderTest, IncomingTopicAllowedUsesConfiguredPatterns) {
  auto forwarder = makeForwarder();

  EXPECT_TRUE(forwarder.isIncomingTopicAllowed("/remote/cmd"));
  EXPECT_FALSE(forwarder.isIncomingTopicAllowed("/blocked/cmd"));
}

TEST_F(TopicForwarderTest, TypeResolutionWorksBeforeAndAfterLocalEndpointAppears) {
  const auto forwarder = makeForwarder();
  const livekit::DataTrackSchemaId string_schema{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};

  // A track may arrive before any application node advertises its ROS topic.
  // Its schema name is a candidate type, but SchemaManager still has to render
  // and compare the exact local definition before a publisher is made.
  EXPECT_EQ(forwarder.resolveInboundRosTopicType("/remote/late", string_schema), "std_msgs/msg/String");

  auto subscription = node_->create_subscription<std_msgs::msg::String>(
      "/remote/late", 10, [](const std_msgs::msg::String::ConstSharedPtr&) {});
  ASSERT_NE(subscription, nullptr);
  ASSERT_TRUE(spinUntil([&]() {
    const auto topics = node_->get_topic_names_and_types();
    return topics.count("/remote/late") > 0U;
  }));

  // Once an endpoint exists, the graph agrees with the schema regardless of
  // arrival order. Conflicting remote metadata cannot override that graph type.
  EXPECT_EQ(forwarder.resolveInboundRosTopicType("/remote/late", string_schema), "std_msgs/msg/String");
  const livekit::DataTrackSchemaId conflicting_schema{"geometry_msgs/msg/Pose",
                                                      livekit::DataTrackSchemaEncoding::Ros2Msg};
  EXPECT_EQ(forwarder.resolveInboundRosTopicType("/remote/late", conflicting_schema), "std_msgs/msg/String");
}

TEST_F(TopicForwarderTest, DiagnosticsWarnsAfterInboundSchemaValidationFailure) {
  auto subscription = node_->create_subscription<std_msgs::msg::String>(
      "/remote/schema_mismatch", 10, [](const std_msgs::msg::String::ConstSharedPtr&) {});
  ASSERT_NE(subscription, nullptr);
  ASSERT_TRUE(spinUntil([&]() {
    const auto topics = node_->get_topic_names_and_types();
    return topics.count("/remote/schema_mismatch") > 0U;
  }));

  auto forwarder = makeForwarder();
  diagnostic_updater::DiagnosticStatusWrapper ok_status;
  forwarder.populateStatus(ok_status);
  EXPECT_EQ(ok_status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(valueFor(ok_status, "inbound_schemas_incorrect"), "0");

  TopicForwarder::RemoteDataTrackDescriptor descriptor;
  descriptor.sid = "schema-mismatch-sid";
  descriptor.track_name = "/remote/schema_mismatch";
  descriptor.publisher_identity = "remote_robot";
  descriptor.schema = livekit::DataTrackSchemaId{"geometry_msgs/msg/Pose", livekit::DataTrackSchemaEncoding::Ros2Msg};
  descriptor.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;

  forwarder.onDataTrackPublished(std::move(descriptor));

  diagnostic_updater::DiagnosticStatusWrapper warn_status;
  forwarder.populateStatus(warn_status);
  EXPECT_EQ(warn_status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(warn_status, "inbound_schemas_incorrect"), "1");
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
TopicForwarder::Options makeRateCapOptions(std::optional<double> max_rate_hz) {
  TopicForwarder::Options options;
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
  livekit_methods.publish_data_track = [push_count](const std::string&, const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
    writer->try_push = [push_count](std::vector<std::uint8_t>) {
      push_count->fetch_add(1);
      return livekit::Result<void, std::string>::success();
    };
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
  };
  livekit_methods.publish_video_track =
      [](const std::string&, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  livekit_methods.schema = makeLiveKitMethods().schema;
  livekit_methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::nullopt;
  };
  return livekit_methods;
}

TopicForwarder::LiveKitMethods makeFlakyLiveKitMethods(std::shared_ptr<std::atomic<int>> push_count,
                                                       std::shared_ptr<std::atomic<int>> remaining_failures) {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track = [push_count, remaining_failures](const std::string&,
                                                                        const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
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
      [](const std::string&, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  livekit_methods.schema = makeLiveKitMethods().schema;
  livekit_methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::nullopt;
  };
  return livekit_methods;
}

// LiveKit callbacks whose data-track writer records the most recent forwarded
// payload (in addition to counting pushes), so tests can assert which cached
// sample the rate timer emitted.
TopicForwarder::LiveKitMethods makeRecordingLiveKitMethods(std::shared_ptr<std::atomic<int>> push_count,
                                                           std::shared_ptr<std::vector<std::uint8_t>> last_payload) {
  TopicForwarder::LiveKitMethods livekit_methods;
  livekit_methods.publish_data_track = [push_count, last_payload](const std::string&, const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
    writer->try_push = [push_count, last_payload](std::vector<std::uint8_t> payload) {
      *last_payload = std::move(payload);
      push_count->fetch_add(1);
      return livekit::Result<void, std::string>::success();
    };
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
  };
  livekit_methods.publish_video_track =
      [](const std::string&, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unused");
  };
  livekit_methods.schema = makeLiveKitMethods().schema;
  livekit_methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::nullopt;
  };
  return livekit_methods;
}

} // namespace

// A rate-capped topic forwards on arrival but drops samples that arrive within
// one period of the last forwarded one (ros-tooling/topic_tools `throttle
// messages`). A dense burst therefore collapses to far fewer forwarded samples
// than were published — most of the burst lands inside a single period.
TEST_F(TopicForwarderTest, RateCapDropsSamplesWithinPeriod) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  // 20 Hz cap -> one sample every 50 ms at most.
  TopicForwarder forwarder(makeRateCapOptions(20.0), node_, makeCountingLiveKitMethods(push_count), diagnostics_fns_);

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

  // The burst is delivered in a tight window, so only the first sample (and at
  // most a couple more, if delivery straddles a period boundary) is forwarded.
  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }));
  spinUntil([]() { return false; }, 175ms);
  EXPECT_GE(push_count->load(), 1);
  EXPECT_LE(push_count->load(), 8);
  EXPECT_LT(push_count->load(), kSamples);
}

// The first sample received in a period is the one forwarded (not the newest):
// samples are gated on arrival, so once "first" passes, later samples arriving
// within the same period are dropped. Delivery is synchronized on the first
// forward so the subscriber's KEEP_LAST queue cannot silently drop "first"
// before the callback sees it.
TEST_F(TopicForwarderTest, RateCapForwardsFirstSampleInPeriod) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  auto last_payload = std::make_shared<std::vector<std::uint8_t>>();
  // 5 Hz cap -> 200 ms period, ample room to send the later samples in-period.
  TopicForwarder forwarder(makeRateCapOptions(5.0), node_, makeRecordingLiveKitMethods(push_count, last_payload),
                           diagnostics_fns_);

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String first_msg;
  first_msg.data = "first";
  publisher->publish(first_msg);

  // Wait until "first" has been forwarded, opening the period.
  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 1; }));

  // "second" and "third" arrive within the same 200 ms period and are dropped.
  for (const auto* text : {"second", "third"}) {
    std_msgs::msg::String msg;
    msg.data = text;
    publisher->publish(msg);
  }
  spinUntil([]() { return false; }, 100ms);

  EXPECT_EQ(push_count->load(), 1);

  // The forwarded payload must decode to the first sample. Generic
  // subscriptions may include trailing CDR alignment padding that a direct
  // serialize_message() call omits, so compare deserialized content.
  ASSERT_FALSE(last_payload->empty());
  rclcpp::SerializedMessage serialized(last_payload->size());
  auto& rcl_msg = serialized.get_rcl_serialized_message();
  std::copy(last_payload->begin(), last_payload->end(), rcl_msg.buffer);
  rcl_msg.buffer_length = last_payload->size();

  rclcpp::Serialization<std_msgs::msg::String> serializer;
  std_msgs::msg::String forwarded_msg;
  serializer.deserialize_message(&serialized, &forwarded_msg);
  EXPECT_EQ(forwarded_msg.data, first_msg.data);
}

// Once a period elapses, the next arriving sample is forwarded again. An idle
// topic is never re-sent: without new input nothing is forwarded (the cap is
// not a rate floor).
TEST_F(TopicForwarderTest, RateCapForwardsAgainAfterPeriodElapses) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  // 20 Hz cap -> 50 ms period.
  TopicForwarder forwarder(makeRateCapOptions(20.0), node_, makeCountingLiveKitMethods(push_count), diagnostics_fns_);

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

  // Stay idle well past one period: no new samples arrive, so nothing more is
  // forwarded.
  spinUntil([]() { return false; }, 150ms);
  EXPECT_EQ(push_count->load(), 1);

  // A fresh sample after the period elapsed is forwarded.
  publisher->publish(msg);
  ASSERT_TRUE(spinUntil([&]() { return push_count->load() >= 2; }));
  EXPECT_EQ(push_count->load(), 2);
}

// A failed push is dropped, not retried: the throttle window advances when the
// sample is passed through, and there is no timer to re-attempt a failed send.
TEST_F(TopicForwarderTest, RateCapDropsFailedPushWithoutRetry) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  auto remaining_failures = std::make_shared<std::atomic<int>>(1);
  // 50 Hz -> 20 ms period; the single sample is passed through immediately.
  TopicForwarder forwarder(makeRateCapOptions(50.0), node_, makeFlakyLiveKitMethods(push_count, remaining_failures),
                           diagnostics_fns_);

  rclcpp::QoS pub_qos{rclcpp::KeepLast(10)};
  pub_qos.reliable();
  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", pub_qos);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1u));

  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1u; }));

  std_msgs::msg::String msg;
  msg.data = "x";
  publisher->publish(msg);

  // The push fails once; with no new samples and no retry, nothing is forwarded.
  spinUntil([]() { return false; }, 150ms);
  EXPECT_EQ(push_count->load(), 0);
}

TEST_F(TopicForwarderTest, OutboundSkipsSampleWhenWriterCreationFails) {
  auto publish_attempts = std::make_shared<std::atomic<int>>(0);
  auto methods = makeLiveKitMethods();
  methods.publish_data_track = [publish_attempts](const std::string&, const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    publish_attempts->fetch_add(1);
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure(
        "required schema unavailable");
  };
  TopicForwarder forwarder(makeRateCapOptions(std::nullopt), node_, std::move(methods), diagnostics_fns_);

  auto publisher = node_->create_publisher<std_msgs::msg::String>("/allowed/data", 10);
  ASSERT_TRUE(waitForPublishers("/allowed/data", 1U));
  forwarder.pollTopics();
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_subscription_count() >= 1U; }));

  std_msgs::msg::String msg;
  msg.data = "must not be forwarded";
  publisher->publish(msg);

  ASSERT_TRUE(spinUntil([&]() { return publish_attempts->load() >= 1; }));
  EXPECT_EQ(publish_attempts->load(), 1);
}

// Without a configured cap, every delivered sample is forwarded. Publishing one
// at a time and spinning between sends keeps the samples from being coalesced by
// the reader's KEEP_LAST queue, isolating the "no throttling" behaviour under test.
TEST_F(TopicForwarderTest, UncappedTopicForwardsEverySample) {
  auto push_count = std::make_shared<std::atomic<int>>(0);
  TopicForwarder forwarder(makeRateCapOptions(std::nullopt), node_, makeCountingLiveKitMethods(push_count),
                           diagnostics_fns_);

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
