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

#pragma once

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"

#include "test_common.hpp"

#include <gtest/gtest.h>

#include <livekit/livekit.h>

#include <rclcpp/rclcpp.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_interface_show.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_service_list.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_topic_list.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace ros2_livekit_bridge::test
{

using namespace std::chrono_literals;
using Ros2InterfaceShow = ros2_livekit_bridge_msgs::srv::Ros2InterfaceShow;
using Ros2ServiceList = ros2_livekit_bridge_msgs::srv::Ros2ServiceList;
using Ros2TopicList = ros2_livekit_bridge_msgs::srv::Ros2TopicList;

inline constexpr auto kGraphTimeout = 15s;
inline constexpr auto kMessageTimeout = 20s;
inline constexpr auto kNegativeAssertionTimeout = 3s;
inline constexpr const char * kBidirectionalTopic = "/bridge/out";

struct TopicListServiceOptions
{
  bool verbose{false};
  std::uint8_t timeout_sec{5};
  bool show_types{false};
  bool count_topics{false};
  bool include_hidden_topics{false};
};

struct ServiceListServiceOptions
{
  std::uint8_t timeout_sec{5};
  bool show_types{false};
  bool count_services{false};
  bool include_hidden_services{false};
};

struct InterfaceShowServiceOptions
{
  std::uint8_t timeout_sec{5};
  bool all_comments{false};
  bool no_comments{false};
};

inline bool contains(const std::string & value, const std::string & needle)
{
  return value.find(needle) != std::string::npos;
}

inline std::size_t lineCount(const std::string & value)
{
  return static_cast<std::size_t>(
    std::count(value.begin(), value.end(), '\n'));
}

inline bool serviceExists(rclcpp::Node & node, const std::string & service_name)
{
  const auto service_names_and_types = node.get_service_names_and_types();
  return std::any_of(
    service_names_and_types.begin(),
    service_names_and_types.end(),
    [&](const auto & name_and_types) {
      return name_and_types.first == service_name;
    });
}

inline rclcpp::NodeOptions createBridgeOptions(
  const rclcpp::Context::SharedPtr & context,
  const std::string & node_namespace,
  const std::string & config_path)
{
  return rclcpp::NodeOptions()
         .context(context)
         .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})
         .parameter_overrides({
      rclcpp::Parameter("config_path", config_path),
    });
}

inline std::string bridgeConfigYaml(
  const std::string & room_name,
  const std::string & topic_pattern)
{
  std::ostringstream stream;
  stream
    << "ros2_livekit_bridge:\n"
    << "  version: \"0.0.1\"\n"
    << "  room_name: \"" << room_name << "\"\n"
    << "  topic_polling_period_ms: 50\n"
    << "  ros_threads: 4\n"
    << "  topics:\n"
    << "    - topic: \"" << topic_pattern << "\"\n"
    << "      direction: \"bidirectional\"\n";
  return stream.str();
}

inline std::string testLiveKitRoom()
{
  std::string source;
  const auto room = utils::resolveEnvironmentCredential("LIVEKIT_ROOM", source);
  if (!room.empty()) {
    return room;
  }
  return "ros_bridge_participant_id_test";
}

class BridgeTestE2E : public ::testing::Test
{
protected:
  // The LiveKit SDK lifecycle is process-global, so it is owned by the test
  // harness rather than by individual bridges: initialize once before any
  // bridge in the suite is constructed, and shut down once after the last test.
  static void SetUpTestSuite()
  {
    livekit::initialize(livekit::LogLevel::Info);
  }

  static void TearDownTestSuite()
  {
    livekit::shutdown();
  }

  void SetUp() override
  {
    std::string source;
    livekit_url_ = utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    token_a_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_A", source);
    token_b_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_B", source);

    const auto original_token =
      utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", source);
    original_token_ = original_token.empty() ?
      std::nullopt : std::optional<std::string>(original_token);

    const auto original_livekit_url =
      utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    original_livekit_url_ = original_livekit_url.empty() ?
      std::nullopt : std::optional<std::string>(original_livekit_url);

    identity_a_ =
      utils::resolveEnvironmentCredential("LIVEKIT_IDENTITY_A", source);
    if (identity_a_.empty()) {
      identity_a_ = "bridge-test-a";
    }
    identity_b_ =
      utils::resolveEnvironmentCredential("LIVEKIT_IDENTITY_B", source);
    if (identity_b_.empty()) {
      identity_b_ = "bridge-test-b";
    }
  }

  void TearDown() override
  {
    shutdownRuntime();
    restoreEnv("LIVEKIT_TOKEN", original_token_);
    restoreEnv("LIVEKIT_URL", original_livekit_url_);
  }

  bool configured() const
  {
    return !livekit_url_.empty() && !token_a_.empty() && !token_b_.empty();
  }

  void initializeRuntime(const std::string & topic_pattern)
  {
    initializeRuntime(topic_pattern, topic_pattern, topic_pattern, topic_pattern);
  }

  void initializeRuntime(
    const std::string & topic_pattern_a,
    const std::string & topic_pattern_b,
    const std::string & publish_topic_a,
    const std::string & publish_topic_b)
  {
    ASSERT_TRUE(configured()) <<
      "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

    const auto [domain_id_a, domain_id_b] = testDomainIds();
    ASSERT_NE(domain_id_a, domain_id_b);
    graph_a_ = std::make_unique<ScopedRosGraph>(domain_id_a);
    graph_b_ = std::make_unique<ScopedRosGraph>(domain_id_b);
    SCOPED_TRACE(
      "ROS graph A domain_id=" + std::to_string(graph_a_->domain_id()) +
      ", ROS graph B domain_id=" + std::to_string(graph_b_->domain_id()));

    config_file_a_ = std::make_unique<TemporaryConfigFile>(
      bridgeConfigYaml(testLiveKitRoom(), topic_pattern_a),
      "ros2_livekit_bridge_bridge_test_e2e_a_");
    config_file_b_ = std::make_unique<TemporaryConfigFile>(
      bridgeConfigYaml(testLiveKitRoom(), topic_pattern_b),
      "ros2_livekit_bridge_bridge_test_e2e_b_");

    bridge_a_ = createBridge(
      *graph_a_, "/bridge_a_node", token_a_, config_file_a_->path().string());
    bridge_b_ = createBridge(
      *graph_b_, "/bridge_b_node", token_b_, config_file_b_->path().string());
    ASSERT_NE(bridge_a_, nullptr);
    ASSERT_NE(bridge_b_, nullptr);

    robot_a_node_ = std::make_shared<rclcpp::Node>(
      "participant_id_bridge_integration_robot_a",
      rclcpp::NodeOptions().context(graph_a_->context()));
    robot_b_node_ = std::make_shared<rclcpp::Node>(
      "participant_id_bridge_integration_robot_b",
      rclcpp::NodeOptions().context(graph_b_->context()));

    graph_a_executor_ =
      std::make_unique<rclcpp::executors::MultiThreadedExecutor>(
      executorOptions(graph_a_->context()), 2);
    graph_b_executor_ =
      std::make_unique<rclcpp::executors::MultiThreadedExecutor>(
      executorOptions(graph_b_->context()), 2);

    graph_a_executor_->add_node(bridge_a_);
    graph_a_executor_->add_node(robot_a_node_);
    graph_b_executor_->add_node(bridge_b_);
    graph_b_executor_->add_node(robot_b_node_);

    graph_a_spinning_.store(true);
    graph_b_spinning_.store(true);
    graph_a_spin_thread_ = std::thread([this]() {
          graph_a_executor_->spin();
          graph_a_spinning_.store(false);
    });
    graph_b_spin_thread_ = std::thread([this]() {
          graph_b_executor_->spin();
          graph_b_spinning_.store(false);
    });

    publisher_a_ =
      robot_a_node_->create_publisher<std_msgs::msg::String>(publish_topic_a, 10);
    publisher_b_ =
      robot_b_node_->create_publisher<std_msgs::msg::String>(publish_topic_b, 10);

    ASSERT_TRUE(waitFor(
        [&]() {
          return topicExists(*robot_a_node_, publish_topic_a) &&
                 topicExists(*robot_b_node_, publish_topic_b);
      },
      kGraphTimeout));
  }

  bool verifyDirection(
    const std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> & publisher,
    const std::shared_ptr<rclcpp::Node> & receiver_node,
    const std::string & source_topic,
    const std::string & expected_inbound_topic,
    const std::string & expected_payload)
  {
    if (!waitFor([&]() {return publisher->get_subscription_count() > 0;}, kGraphTimeout)) {
      ADD_FAILURE() << "Bridge did not subscribe to " << source_topic;
      return false;
    }

    std::optional<std::string> inbound_topic;
    if (!waitFor(
        [&]() {
          publisher->publish(makeMessage("warmup:" + expected_payload));
          inbound_topic = findParticipantPrefixedTopic(*receiver_node, source_topic);
          return inbound_topic.has_value();
        },
        kGraphTimeout))
    {
      ADD_FAILURE()
        << "No participant-prefixed ROS topic appeared for " << source_topic;
      return false;
    }

    if (!inbound_topic.has_value()) {
      ADD_FAILURE() << "No inbound topic captured for " << source_topic;
      return false;
    }
    if (*inbound_topic == source_topic) {
      ADD_FAILURE() << "Inbound topic was not participant-prefixed: "
                    << *inbound_topic;
      return false;
    }
    if (*inbound_topic != expected_inbound_topic) {
      ADD_FAILURE() << "Inbound topic did not use expected participant "
        "prefix. Expected "
                    << expected_inbound_topic << ", got " << *inbound_topic;
      return false;
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::optional<std::string> received_payload;

    auto subscription = receiver_node->create_subscription<std_msgs::msg::String>(
      *inbound_topic,
      10,
      [&](const std_msgs::msg::String::ConstSharedPtr msg) {
        if (msg->data != expected_payload) {
          return;
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          received_payload = msg->data;
        }
        cv.notify_all();
      });

    const auto deadline = std::chrono::steady_clock::now() + kMessageTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
      publisher->publish(makeMessage(expected_payload));

      std::unique_lock<std::mutex> lock(mutex);
      if (cv.wait_for(lock, 100ms, [&]() {return received_payload.has_value();})) {
        break;
      }
    }

    subscription.reset();
    if (received_payload != expected_payload) {
      ADD_FAILURE() << "Did not receive payload on " << *inbound_topic;
      return false;
    }
    return true;
  }

  bool verifyDirectionNotForwarded(
    const std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> & publisher,
    const std::shared_ptr<rclcpp::Node> & receiver_node,
    const std::string & source_topic,
    const std::string & forbidden_inbound_topic,
    const std::string & payload)
  {
    if (!waitFor([&]() {return publisher->get_subscription_count() > 0;}, kGraphTimeout)) {
      ADD_FAILURE() << "Bridge did not subscribe to " << source_topic;
      return false;
    }

    std::atomic_bool received_forbidden_payload{false};
    auto subscription = receiver_node->create_subscription<std_msgs::msg::String>(
      forbidden_inbound_topic,
      10,
      [&](const std_msgs::msg::String::ConstSharedPtr msg) {
        if (msg->data == payload) {
          received_forbidden_payload.store(true);
        }
      });

    const auto deadline =
      std::chrono::steady_clock::now() + kNegativeAssertionTimeout;
    while (
      std::chrono::steady_clock::now() < deadline &&
      !received_forbidden_payload.load())
    {
      publisher->publish(makeMessage(payload));
      std::this_thread::sleep_for(100ms);
    }

    subscription.reset();
    if (received_forbidden_payload.load()) {
      ADD_FAILURE() << "Unexpectedly received payload on forbidden topic "
                    << forbidden_inbound_topic;
      return false;
    }
    return true;
  }

  std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> publisherA() const
  {
    return publisher_a_;
  }

  std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> publisherB() const
  {
    return publisher_b_;
  }

  Ros2TopicList::Response::SharedPtr callTopicListService(
    const std::shared_ptr<rclcpp::Node> & node,
    const std::string & participant_id,
    const TopicListServiceOptions & options = {})
  {
    auto client = node->create_client<Ros2TopicList>(
      "/ros2_livekit_bridge/ros2_topic_list");

    if (!waitFor(
        [&]() {
          return client->wait_for_service(100ms);
        },
        kGraphTimeout))
    {
      ADD_FAILURE() << "ros2_topic_list service was not available";
      return nullptr;
    }

    auto request = std::make_shared<Ros2TopicList::Request>();
    request->participant_id = participant_id;
    request->show_types = options.show_types;
    request->count_topics = options.count_topics;
    request->include_hidden_topics = options.include_hidden_topics;
    request->verbose = options.verbose;
    request->timeout_sec = options.timeout_sec;

    auto future = client->async_send_request(request);
    if (future.wait_for(kMessageTimeout) != std::future_status::ready) {
      ADD_FAILURE() << "ros2_topic_list service timed out";
      return nullptr;
    }

    return future.get();
  }

  Ros2ServiceList::Response::SharedPtr callServiceListService(
    const std::shared_ptr<rclcpp::Node> & node,
    const std::string & participant_id,
    const ServiceListServiceOptions & options = {})
  {
    auto client = node->create_client<Ros2ServiceList>(
      "/ros2_livekit_bridge/ros2_service_list");

    if (!waitFor(
        [&]() {
          return client->wait_for_service(100ms);
        },
        kGraphTimeout))
    {
      ADD_FAILURE() << "ros2_service_list service was not available";
      return nullptr;
    }

    auto request = std::make_shared<Ros2ServiceList::Request>();
    request->participant_id = participant_id;
    request->show_types = options.show_types;
    request->count_services = options.count_services;
    request->include_hidden_services = options.include_hidden_services;
    request->timeout_sec = options.timeout_sec;

    auto future = client->async_send_request(request);
    if (future.wait_for(kMessageTimeout) != std::future_status::ready) {
      ADD_FAILURE() << "ros2_service_list service timed out";
      return nullptr;
    }

    return future.get();
  }

  Ros2InterfaceShow::Response::SharedPtr callInterfaceShowService(
    const std::shared_ptr<rclcpp::Node> & node,
    const std::string & participant_id,
    const std::string & interface_type,
    const InterfaceShowServiceOptions & options = {})
  {
    auto client = node->create_client<Ros2InterfaceShow>(
      "/ros2_livekit_bridge/ros2_interface_show");

    if (!waitFor(
        [&]() {
          return client->wait_for_service(100ms);
        },
        kGraphTimeout))
    {
      ADD_FAILURE() << "ros2_interface_show service was not available";
      return nullptr;
    }

    auto request = std::make_shared<Ros2InterfaceShow::Request>();
    request->participant_id = participant_id;
    request->type = interface_type;
    request->all_comments = options.all_comments;
    request->no_comments = options.no_comments;
    request->timeout_sec = options.timeout_sec;

    auto future = client->async_send_request(request);
    if (future.wait_for(kMessageTimeout) != std::future_status::ready) {
      ADD_FAILURE() << "ros2_interface_show service timed out";
      return nullptr;
    }

    return future.get();
  }

  std::shared_ptr<rclcpp::Node> robotANode() const {return robot_a_node_;}
  std::shared_ptr<rclcpp::Node> robotBNode() const {return robot_b_node_;}
  const std::string & identityA() const {return identity_a_;}
  const std::string & identityB() const {return identity_b_;}

private:
  static std_msgs::msg::String makeMessage(const std::string & data)
  {
    std_msgs::msg::String msg;
    msg.data = data;
    return msg;
  }

  rclcpp::ExecutorOptions executorOptions(
    const rclcpp::Context::SharedPtr & context) const
  {
    auto options = rclcpp::ExecutorOptions{};
    options.context = context;
    return options;
  }

  std::shared_ptr<Ros2LiveKitBridge> createBridge(
    const ScopedRosGraph & graph,
    const std::string & node_namespace,
    const std::string & livekit_token,
    const std::string & config_path)
  {
    if (!setEnv("LIVEKIT_TOKEN", livekit_token)) {
      ADD_FAILURE() << "Failed to set environment variable LIVEKIT_TOKEN";
      return nullptr;
    }
    if (!setEnv("LIVEKIT_URL", livekit_url_)) {
      ADD_FAILURE() << "Failed to set environment variable LIVEKIT_URL";
      return nullptr;
    }

    auto bridge = std::make_shared<Ros2LiveKitBridge>(
      createBridgeOptions(graph.context(), node_namespace, config_path));
    if (!bridge->initialize()) {
      ADD_FAILURE() << "Bridge failed to initialize for namespace "
                    << node_namespace;
      return nullptr;
    }
    return bridge;
  }

  void shutdownRuntime()
  {
    if (graph_a_executor_ && graph_a_spinning_.exchange(false)) {
      graph_a_executor_->cancel();
    }
    if (graph_b_executor_ && graph_b_spinning_.exchange(false)) {
      graph_b_executor_->cancel();
    }

    if (graph_a_spin_thread_.joinable()) {
      graph_a_spin_thread_.join();
    }
    if (graph_b_spin_thread_.joinable()) {
      graph_b_spin_thread_.join();
    }

    if (graph_b_executor_) {
      if (robot_b_node_) {
        graph_b_executor_->remove_node(robot_b_node_);
      }
      if (bridge_b_) {
        graph_b_executor_->remove_node(bridge_b_);
      }
    }
    if (graph_a_executor_) {
      if (robot_a_node_) {
        graph_a_executor_->remove_node(robot_a_node_);
      }
      if (bridge_a_) {
        graph_a_executor_->remove_node(bridge_a_);
      }
    }

    publisher_a_.reset();
    publisher_b_.reset();
    robot_a_node_.reset();
    robot_b_node_.reset();
    bridge_a_.reset();
    bridge_b_.reset();
    graph_a_executor_.reset();
    graph_b_executor_.reset();
    graph_a_.reset();
    graph_b_.reset();
    config_file_a_.reset();
    config_file_b_.reset();
  }

  std::string livekit_url_;
  std::string token_a_;
  std::string token_b_;
  std::optional<std::string> original_token_;
  std::optional<std::string> original_livekit_url_;
  std::string identity_a_;
  std::string identity_b_;

  std::unique_ptr<ScopedRosGraph> graph_a_;
  std::unique_ptr<ScopedRosGraph> graph_b_;
  std::unique_ptr<TemporaryConfigFile> config_file_a_;
  std::unique_ptr<TemporaryConfigFile> config_file_b_;

  std::shared_ptr<Ros2LiveKitBridge> bridge_a_;
  std::shared_ptr<Ros2LiveKitBridge> bridge_b_;
  std::shared_ptr<rclcpp::Node> robot_a_node_;
  std::shared_ptr<rclcpp::Node> robot_b_node_;

  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> graph_a_executor_;
  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> graph_b_executor_;
  std::thread graph_a_spin_thread_;
  std::thread graph_b_spin_thread_;
  std::atomic_bool graph_a_spinning_{false};
  std::atomic_bool graph_b_spinning_{false};

  std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> publisher_a_;
  std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> publisher_b_;
};

}  // namespace ros2_livekit_bridge::test
