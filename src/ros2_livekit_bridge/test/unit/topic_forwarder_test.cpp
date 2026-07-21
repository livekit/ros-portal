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

#include "ros2_livekit_bridge/schema_manager.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

// TopicForwarder now creates its subscriptions and publishers directly on the
// ROS node it is given, so these unit tests cover only the node-independent
// logic (construction validation, incoming-topic matching, and QoS
// negotiation against the live ROS graph). End-to-end forwarding behaviour
// (data push, image capture, inbound republishing) is exercised by the
// integration tests.
namespace ros2_livekit_bridge {
namespace {

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

std::optional<std::string> renderSchemaText(const std::string& topic_type) {
  auto methods = makeLiveKitMethods().schema;
  std::optional<std::string> schema_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string& text) {
    schema_text = text;
    return true;
  };
  SchemaManager manager(std::move(methods));
  if (!manager.ensureSchemaDefined(topic_type)) {
    return std::nullopt;
  }
  return schema_text;
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

TEST_F(TopicForwarderTest, SchemaValidationAcceptsExactMatch) {
  const auto schema_text = renderSchemaText("std_msgs/msg/String");
  if (!schema_text.has_value()) {
    FAIL() << "std_msgs/msg/String schema was unavailable";
    return;
  }

  auto methods = makeLiveKitMethods();
  methods.schema.get_schema = [text = *schema_text](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{text};
  };
  const TopicForwarder forwarder(makeOptions(), node_, std::move(methods));
  TopicForwarder::RemoteDataTrackDescriptor descriptor{
      "sid",
      "/remote/data",
      "publisher",
      livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg},
      livekit::DataTrackFrameEncoding::Cdr,
      {},
  };

  const auto result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.reason.empty());
  EXPECT_EQ(result.remote_hash, result.local_hash);

  descriptor.frame_encoding = livekit::DataTrackFrameEncoding::Json;
  const auto json_result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_TRUE(json_result.accepted);
  EXPECT_TRUE(json_result.reason.empty());
  EXPECT_EQ(json_result.remote_hash, json_result.local_hash);
}

TEST_F(TopicForwarderTest, SchemaValidationRejectsMissingMetadata) {
  const auto forwarder = makeForwarder();
  TopicForwarder::RemoteDataTrackDescriptor descriptor{"sid",        "/remote/data", "publisher",
                                                       std::nullopt, std::nullopt,   {}};

  auto result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("frame encoding"), std::string::npos);

  descriptor.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("schema"), std::string::npos);
}

TEST_F(TopicForwarderTest, SchemaValidationRejectsWrongTypeAndEncoding) {
  const auto forwarder = makeForwarder();
  TopicForwarder::RemoteDataTrackDescriptor descriptor{
      "sid",
      "/remote/data",
      "publisher",
      livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::JsonSchema},
      livekit::DataTrackFrameEncoding::Cdr,
      {},
  };

  auto result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("schema encoding"), std::string::npos);

  descriptor.schema = livekit::DataTrackSchemaId{"geometry_msgs/msg/Pose", livekit::DataTrackSchemaEncoding::Ros2Msg};
  result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("does not match local ROS type"), std::string::npos);

  descriptor.schema = livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};
  descriptor.frame_encoding = livekit::DataTrackFrameEncoding::Protobuf;
  result = forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("not CDR or JSON"), std::string::npos);
}

TEST_F(TopicForwarderTest, SchemaValidationRejectsRetrievalAndRenderFailures) {
  auto methods = makeLiveKitMethods();
  methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  const TopicForwarder retrieval_failure_forwarder(makeOptions(), node_, std::move(methods));
  TopicForwarder::RemoteDataTrackDescriptor descriptor{
      "sid",
      "/remote/data",
      "publisher",
      livekit::DataTrackSchemaId{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg},
      livekit::DataTrackFrameEncoding::Cdr,
      {},
  };

  auto result = retrieval_failure_forwarder.validateInboundSchema(descriptor, "std_msgs/msg/String");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("retrieval failed"), std::string::npos);

  methods = makeLiveKitMethods();
  methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"string data\n"};
  };
  const TopicForwarder render_failure_forwarder(makeOptions(), node_, std::move(methods));
  descriptor.schema =
      livekit::DataTrackSchemaId{"nonexistent_pkg/msg/DoesNotExist", livekit::DataTrackSchemaEncoding::Ros2Msg};
  result = render_failure_forwarder.validateInboundSchema(descriptor, "nonexistent_pkg/msg/DoesNotExist");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("could not be rendered"), std::string::npos);
}

TEST_F(TopicForwarderTest, SchemaValidationRejectsRootAndNestedMismatches) {
  const auto schema_text = renderSchemaText("geometry_msgs/msg/PoseStamped");
  if (!schema_text.has_value()) {
    FAIL() << "geometry_msgs/msg/PoseStamped schema was unavailable";
    return;
  }
  const auto original_text = *schema_text;
  auto remote_text = std::make_shared<std::string>(*schema_text);

  auto methods = makeLiveKitMethods();
  methods.schema.get_schema = [remote_text](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{*remote_text};
  };
  const TopicForwarder forwarder(makeOptions(), node_, std::move(methods));
  const TopicForwarder::RemoteDataTrackDescriptor descriptor{
      "sid",
      "/remote/data",
      "publisher",
      livekit::DataTrackSchemaId{"geometry_msgs/msg/PoseStamped", livekit::DataTrackSchemaEncoding::Ros2Msg},
      livekit::DataTrackFrameEncoding::Cdr,
      {},
  };

  const auto root_field = remote_text->find("Pose pose");
  ASSERT_NE(root_field, std::string::npos);
  remote_text->replace(root_field, std::string("Pose pose").size(), "Pose changed_pose");
  auto result = forwarder.validateInboundSchema(descriptor, "geometry_msgs/msg/PoseStamped");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("definitions differ"), std::string::npos);

  *remote_text = original_text;
  const auto nested_field = remote_text->find("float64 x");
  ASSERT_NE(nested_field, std::string::npos);
  remote_text->replace(nested_field, std::string("float64 x").size(), "float32 x");
  result = forwarder.validateInboundSchema(descriptor, "geometry_msgs/msg/PoseStamped");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("definitions differ"), std::string::npos);
}

TEST_F(TopicForwarderTest, TypeResolutionWorksBeforeAndAfterLocalEndpointAppears) {
  const auto forwarder = makeForwarder();
  const livekit::DataTrackSchemaId string_schema{"std_msgs/msg/String", livekit::DataTrackSchemaEncoding::Ros2Msg};

  // A track may arrive before any application node advertises its ROS topic.
  // Its schema name is a candidate type, but validateInboundSchema still has to
  // render and compare the exact local definition before a publisher is made.
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
  TopicForwarder forwarder(makeRateCapOptions(5.0), node_, makeRecordingLiveKitMethods(push_count, last_payload));

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
  TopicForwarder forwarder(makeRateCapOptions(20.0), node_, makeCountingLiveKitMethods(push_count));

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
  TopicForwarder forwarder(makeRateCapOptions(std::nullopt), node_, std::move(methods));

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
