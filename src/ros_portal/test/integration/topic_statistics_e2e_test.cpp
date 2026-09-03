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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <statistics_msgs/msg/metrics_message.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "diagnostics_test_utils.hpp"
#include "ros_portal/latched_topic_forwarder.hpp"
#include "ros_portal/topic_forwarder.hpp"
#include "ros_portal/utils/config_mapping.hpp"
#include "ros_portal_config/config/config_parser.hpp"
#include "test_common.hpp"

namespace ros_portal::test {
namespace {

using namespace std::chrono_literals;

/// @brief rclcpp's default statistics stream, shared by every subscription.
/// ROS Portal overrides `topic_stats_options.publish_topic` so measurements land
/// on a per-topic child stream instead, which must leave this one unused.
constexpr char kGlobalStatisticsTopic[] = "/statistics";
constexpr auto kGraphTimeout = 5s;
constexpr auto kStatisticsTimeout = 4s;

TopicForwarder::LiveKitMethods makeUnavailableLiveKitMethods() {
  TopicForwarder::LiveKitMethods methods;
  methods.is_room_available = []() { return false; };
  methods.publish_data_track = [](const std::string&, const livekit::DataTrackSchemaId&)
      -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure("unavailable");
  };
  methods.publish_video_track =
      [](const std::string&, int,
         int) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
    return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure("unavailable");
  };
  methods.schema.define_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return false; };
  methods.schema.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  return methods;
}

LatchedTopicForwarder::LiveKitMethods makeUnavailableLatchedLiveKitMethods() {
  LatchedTopicForwarder::LiveKitMethods methods;
  methods.is_room_available = []() { return false; };
  methods.register_rpc_method = [](const std::string&, RpcHandler) { return true; };
  methods.unregister_rpc_method = [](const std::string&) { return true; };
  methods.perform_rpc = [](const std::string&, const std::string&, const std::string&,
                           std::uint8_t) -> std::optional<std::string> { return std::nullopt; };
  methods.list_remote_identities = []() { return std::vector<std::string>{}; };
  return methods;
}

class TopicStatisticsE2E : public ::testing::Test {
protected:
  void SetUp() override {
    graph_ = std::make_unique<ScopedRosGraph>(testDomainIds().first);
    const auto node_options = rclcpp::NodeOptions().context(graph_->context());
    forwarder_node_ = std::make_shared<rclcpp::Node>("topic_statistics_forwarder", node_options);
    publisher_node_ = std::make_shared<rclcpp::Node>("topic_statistics_publishers", node_options);
    observer_node_ = std::make_shared<rclcpp::Node>("topic_statistics_observer", node_options);

    diagnostics_updater_ = std::make_shared<diagnostic_updater::Updater>(forwarder_node_);
    diagnostics_updater_->setHardwareID("ros_portal");
    diagnostics_fns_ = makeDiagnosticsFns(diagnostics_updater_);

    auto executor_options = rclcpp::ExecutorOptions{};
    executor_options.context = graph_->context();
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
    executor_->add_node(forwarder_node_);
    executor_->add_node(publisher_node_);
    executor_->add_node(observer_node_);
  }

  void TearDown() override {
    forwarder_.reset();
    statistics_subscriptions_.clear();
    statistics_counts_.clear();
    diagnostics_fns_ = {};
    diagnostics_updater_.reset();
    executor_.reset();
    observer_node_.reset();
    publisher_node_.reset();
    forwarder_node_.reset();
    graph_.reset();
  }

  void configureForwarder(const std::string& yaml) {
    const auto config = ros_portal_config::ConfigParser{}.parseString(yaml);
    auto options = utils::topicForwarderOptions(config.topics, config.enable_all_ros_topic_stats, 1, 10, {},
                                                forwarder_node_->get_logger());
    forwarder_ = std::make_unique<TopicForwarder>(std::move(options), forwarder_node_, makeUnavailableLiveKitMethods(),
                                                  diagnostics_fns_);
  }

  template <typename Predicate>
  bool spinUntil(Predicate&& predicate, std::chrono::milliseconds timeout = kGraphTimeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      executor_->spin_some();
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    executor_->spin_some();
    return predicate();
  }

  void reconcileWhenTopicsAreVisible(const std::vector<std::string>& topics) {
    ASSERT_TRUE(spinUntil([&]() {
      const auto graph_topics = forwarder_node_->get_topic_names_and_types();
      return std::all_of(topics.begin(), topics.end(),
                         [&](const std::string& topic) { return graph_topics.count(topic) > 0U; });
    }));
    forwarder_->reconcileTopics(forwarder_node_->get_topic_names_and_types());
  }

  /// @brief Start counting MetricsMessages arriving on @p statistics_topic.
  void observeStatistics(const std::string& statistics_topic) {
    auto received = std::make_shared<std::atomic_size_t>(0U);
    statistics_counts_[statistics_topic] = received;
    statistics_subscriptions_.push_back(observer_node_->create_subscription<statistics_msgs::msg::MetricsMessage>(
        statistics_topic, 10,
        [received](const statistics_msgs::msg::MetricsMessage::ConstSharedPtr&) { received->fetch_add(1); }));
  }

  /// @brief MetricsMessages seen on @p statistics_topic, 0 when unobserved.
  std::size_t statisticsCount(const std::string& statistics_topic) const {
    const auto it = statistics_counts_.find(statistics_topic);
    return it == statistics_counts_.end() ? 0U : it->second->load();
  }

  /// @brief Publish on @p publisher until @p statistics_topic delivers a sample.
  bool publishUntilStatistics(const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr& publisher,
                              const std::string& statistics_topic) {
    std_msgs::msg::String message;
    message.data = "topic statistics sample";
    return spinUntil(
        [&]() {
          publisher->publish(message);
          return statisticsCount(statistics_topic) > 0U;
        },
        kStatisticsTimeout);
  }

  std::unique_ptr<ScopedRosGraph> graph_;
  std::shared_ptr<rclcpp::Node> forwarder_node_;
  std::shared_ptr<rclcpp::Node> publisher_node_;
  std::shared_ptr<rclcpp::Node> observer_node_;
  std::shared_ptr<diagnostic_updater::Updater> diagnostics_updater_;
  diagnostics::DiagnosticsManagerFns diagnostics_fns_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::unique_ptr<TopicForwarder> forwarder_;
  std::vector<rclcpp::Subscription<statistics_msgs::msg::MetricsMessage>::SharedPtr> statistics_subscriptions_;
  std::unordered_map<std::string, std::shared_ptr<std::atomic_size_t>> statistics_counts_;
};

TEST_F(TopicStatisticsE2E, GlobalSettingProducesStatisticsForAllTopics) {
  configureForwarder(R"(
ros_portal:
  version: "0.0.1"
  enable_all_ros_topic_stats: true
  topics:
    - topic: "/stats/global/one"
      direction: "out"
    - topic: "/stats/global/two"
      direction: "out"
)");

  auto publisher_one = publisher_node_->create_publisher<std_msgs::msg::String>("/stats/global/one", 10);
  auto publisher_two = publisher_node_->create_publisher<std_msgs::msg::String>("/stats/global/two", 10);
  observeStatistics("/stats/global/one/statistics");
  observeStatistics("/stats/global/two/statistics");
  reconcileWhenTopicsAreVisible({"/stats/global/one", "/stats/global/two"});

  // Each measured topic gets its own child stream rather than sharing one.
  ASSERT_TRUE(spinUntil([&]() {
    return publisher_one->get_subscription_count() == 1U && publisher_two->get_subscription_count() == 1U &&
           observer_node_->count_publishers("/stats/global/one/statistics") == 1U &&
           observer_node_->count_publishers("/stats/global/two/statistics") == 1U;
  }));
  EXPECT_EQ(observer_node_->count_publishers(kGlobalStatisticsTopic), 0U)
      << "measurements must not fall back to rclcpp's shared /statistics stream";

  EXPECT_TRUE(publishUntilStatistics(publisher_one, "/stats/global/one/statistics"));
  EXPECT_TRUE(publishUntilStatistics(publisher_two, "/stats/global/two/statistics"));
}

TEST_F(TopicStatisticsE2E, PerTopicSettingCreatesStatisticsOnlyForConfiguredTopic) {
  configureForwarder(R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/stats/enabled"
      direction: "out"
      enable_ros_topic_stats: true
    - topic: "/stats/disabled"
      direction: "out"
)");

  auto enabled_publisher = publisher_node_->create_publisher<std_msgs::msg::String>("/stats/enabled", 10);
  auto disabled_publisher = publisher_node_->create_publisher<std_msgs::msg::String>("/stats/disabled", 10);
  observeStatistics("/stats/enabled/statistics");
  observeStatistics("/stats/disabled/statistics");
  reconcileWhenTopicsAreVisible({"/stats/enabled", "/stats/disabled"});

  ASSERT_TRUE(spinUntil([&]() {
    return enabled_publisher->get_subscription_count() == 1U && disabled_publisher->get_subscription_count() == 1U &&
           observer_node_->count_publishers("/stats/enabled/statistics") == 1U;
  }));
  EXPECT_EQ(observer_node_->count_publishers("/stats/disabled/statistics"), 0U)
      << "the topic without enable_ros_topic_stats must not create a statistics publisher";
  EXPECT_EQ(observer_node_->count_publishers(kGlobalStatisticsTopic), 0U)
      << "measurements must not fall back to rclcpp's shared /statistics stream";

  EXPECT_TRUE(publishUntilStatistics(enabled_publisher, "/stats/enabled/statistics"));
  EXPECT_EQ(statisticsCount("/stats/disabled/statistics"), 0U)
      << "the topic without enable_ros_topic_stats must not publish measurements";
}

TEST_F(TopicStatisticsE2E, LatchedTopicProducesPerTopicStatistics) {
  LatchedTopicForwarder::Options options;
  options.outbound_topics.insert("/stats/latched");
  options.ros_topic_stats_topics.insert("/stats/latched");
  options.push_interval = std::chrono::hours(1);
  LatchedTopicForwarder forwarder(std::move(options), forwarder_node_, makeUnavailableLatchedLiveKitMethods(),
                                  diagnostics_fns_);

  auto publisher = publisher_node_->create_publisher<std_msgs::msg::String>(
      "/stats/latched", rclcpp::QoS(10).reliable().transient_local());
  observeStatistics("/stats/latched/statistics");
  forwarder.reconcileTopics({{"/stats/latched", {"std_msgs/msg/String"}}});

  ASSERT_TRUE(spinUntil([&]() {
    return publisher->get_subscription_count() == 1U &&
           observer_node_->count_publishers("/stats/latched/statistics") == 1U;
  }));
  EXPECT_EQ(observer_node_->count_publishers(kGlobalStatisticsTopic), 0U)
      << "latched measurements must not fall back to rclcpp's shared /statistics stream";
  EXPECT_TRUE(publishUntilStatistics(publisher, "/stats/latched/statistics"));
}

TEST_F(TopicStatisticsE2E, StatisticsStreamsAreNotThemselvesMeasured) {
  configureForwarder(R"(
ros_portal:
  version: "0.0.1"
  enable_all_ros_topic_stats: true
  topics:
    - topic: "/stats/.*"
      direction: "out"
)");

  auto publisher = publisher_node_->create_publisher<std_msgs::msg::String>("/stats/recursive", 10);
  observeStatistics("/stats/recursive/statistics");
  reconcileWhenTopicsAreVisible({"/stats/recursive"});

  ASSERT_TRUE(spinUntil([&]() { return observer_node_->count_publishers("/stats/recursive/statistics") == 1U; }));

  // A statistics stream is an ordinary topic, so it matches "/stats/.*" and the
  // next discovery pass subscribes to it. Measuring it would nest another
  // statistics topic one level deeper on every pass.
  reconcileWhenTopicsAreVisible({"/stats/recursive/statistics"});
  EXPECT_TRUE(publishUntilStatistics(publisher, "/stats/recursive/statistics"));
  EXPECT_EQ(observer_node_->count_publishers("/stats/recursive/statistics/statistics"), 0U)
      << "a statistics stream must not have statistics collected about it";
}

} // namespace
} // namespace ros_portal::test
