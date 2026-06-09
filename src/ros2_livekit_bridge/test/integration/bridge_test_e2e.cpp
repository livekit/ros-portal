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

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"

#include "test_common.hpp"

#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

namespace
{

using namespace std::chrono_literals;
using ros2_livekit_bridge::Ros2LiveKitBridge;
using ros2_livekit_bridge::test::getenvString;
using ros2_livekit_bridge::test::restoreEnv;
using ros2_livekit_bridge::test::setEnv;

constexpr auto kGraphTimeout = 15s;
constexpr auto kMessageTimeout = 20s;
constexpr auto kPollInterval = 50ms;
constexpr const char * kBidirectionalTopic = "/bridge/out";

/// Create a ROS node options object for a bridge node
/// @param context The ROS context to use for the node
/// @param node_namespace The namespace to use for the node
/// @param ros_topics The ROS topics to allow the bridge to publish to (ROS -> LiveKit)
/// @param lk_topics The LiveKit topics to allow the bridge to subscribe to (LK -> ROS)
/// @return A ROS node options object
rclcpp::NodeOptions createBridgeOptions(
  // ROS args
  const rclcpp::Context::SharedPtr & context,
  const std::string & node_namespace,
  // Bridge config param args
  const std::vector<std::string> & ros_topics,
  const std::vector<std::string> & lk_topics
)
{
  return rclcpp::NodeOptions()
         .context(context)
         .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})
         .parameter_overrides({
      rclcpp::Parameter("room_name", "integration_test"),
      rclcpp::Parameter("topic_polling_period_ms", 50),
      rclcpp::Parameter("ros_threads", 4),
      rclcpp::Parameter("ros_topics", ros_topics),
      rclcpp::Parameter("lk_topics", lk_topics),
    });
}

template<typename Predicate>
bool waitFor(Predicate && predicate, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return predicate();
}

std::string escapedRegex(const std::string & value)
{
  static const std::regex special_chars{R"([.^$|()\\[\]{}*+?])"};
  return std::regex_replace(value, special_chars, R"(\$&)");
}

std::string sanitizeRosNameToken(const std::string & token)
{
  std::string sanitized;
  sanitized.reserve(token.size());
  for (const unsigned char ch : token) {
    if (std::isalnum(ch) || ch == '_') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return "participant";
  }
  if (std::isdigit(static_cast<unsigned char>(sanitized.front()))) {
    sanitized.insert(sanitized.begin(), '_');
  }
  return sanitized;
}

std::string expectedInboundTopicName(
  const std::string & participant_identity,
  const std::string & source_topic)
{
  return "/" + sanitizeRosNameToken(participant_identity) + source_topic;
}

std::optional<std::string> findParticipantPrefixedTopic(
  const rclcpp::Node & node,
  const std::string & source_topic)
{
  const std::regex topic_regex("^/[^/]+" + escapedRegex(source_topic) + "$");
  const auto topics = node.get_topic_names_and_types();
  for (const auto & [topic_name, topic_types] : topics) {
    if (std::regex_match(topic_name, topic_regex) &&
      topic_name != source_topic &&
      std::find(topic_types.begin(), topic_types.end(),
        "std_msgs/msg/String") != topic_types.end())
    {
      return topic_name;
    }
  }
  return std::nullopt;
}

bool topicExists(const rclcpp::Node & node, const std::string & topic)
{
  const auto topics = node.get_topic_names_and_types();
  return topics.find(topic) != topics.end();
}

std_msgs::msg::String makeMessage(const std::string & data)
{
  std_msgs::msg::String msg;
  msg.data = data;
  return msg;
}

/// Pick two process-scoped ROS domain IDs for this test run.
/// Uses PID-derived values to reduce cross-run DDS collisions while keeping
/// graph A and graph B isolated from each other. Hard-coding domain IDs can
/// make parallel runs interfere and introduce flaky discovery/message leakage.
std::pair<std::size_t, std::size_t> testDomainIds()
{
  const auto pid = static_cast<std::size_t>(::getpid());
  const auto base_domain_id = 20U + ((pid % 40U) * 2U);
  return {base_domain_id, base_domain_id + 1U};
}

/// RAII wrapper for a ROS graph
class ScopedRosGraph
{
public:
  /// Initialize a ROS graph with the given domain ID
  /// Creates a context and initializes it with the given domain ID
  /// The context is used to create ROS nodes and publishers/subscribers
  explicit ScopedRosGraph(std::size_t domain_id)
  : domain_id_(domain_id),
    context_(std::make_shared<rclcpp::Context>())
  {
    rclcpp::InitOptions init_options;
    init_options.set_domain_id(domain_id_);
    context_->init(0, nullptr, init_options);
  }

  /// Shutdown the ROS graph
  /// Shuts down the context and cleans up the ROS graph
  ~ScopedRosGraph()
  {
    if (context_ && rclcpp::ok(context_)) {
      context_->shutdown("test ROS graph shutdown");
    }
  }

  rclcpp::Context::SharedPtr context() const {return context_;}
  std::size_t domain_id() const {return domain_id_;}

private:
  std::size_t domain_id_;
  rclcpp::Context::SharedPtr context_;
};

class BridgeTestE2E : public ::testing::Test
{
protected:
  void SetUp() override
  {
    livekit_url_ = getenvString("LIVEKIT_URL");
    token_a_ = getenvString("LIVEKIT_TOKEN_A");
    token_b_ = getenvString("LIVEKIT_TOKEN_B");
    original_token_ = getenvString("LIVEKIT_TOKEN");
    identity_a_ = getenvString("LIVEKIT_IDENTITY_A").value_or("bridge-test-a");
    identity_b_ = getenvString("LIVEKIT_IDENTITY_B").value_or("bridge-test-b");
  }

  void TearDown() override
  {
    restoreEnv("LIVEKIT_TOKEN", original_token_);
  }

  bool configured() const
  {
    return livekit_url_ && token_a_ && token_b_;
  }

  std::optional<std::string> livekit_url_;
  std::optional<std::string> token_a_;
  std::optional<std::string> token_b_;
  std::optional<std::string> original_token_;
  std::string identity_a_;
  std::string identity_b_;
};

}  // namespace

// End-to-end bridge check: two isolated ROS graphs publish message topics through
// separate bridge participants in the same LiveKit room, then verify each graph
// receives the other's message only via the bridge. This catches regressions in
// LiveKit data-track forwarding, participant topic prefixing, and ROS graph
// isolation without relying on shared local ROS discovery.
TEST_F(
  BridgeTestE2E,
  RepublishesRosMessagesBothWays)
{
  // Ensure environment variables are set before proceeding
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  // Setup domain IDs/graphs for the test
  const auto [domain_id_a, domain_id_b] = testDomainIds();
  ASSERT_NE(domain_id_a, domain_id_b);
  ScopedRosGraph graph_a(domain_id_a);
  ScopedRosGraph graph_b(domain_id_b);
  SCOPED_TRACE(
    "ROS graph A domain_id=" + std::to_string(graph_a.domain_id()) +
    ", ROS graph B domain_id=" + std::to_string(graph_b.domain_id()));

  // Context for the below setup: right now the bridge config only accepts a LIVEKIT_TOKEN
  // env var, since only a single bridge is needed on a compute in production. Instead of
  // adding another parameter for testing, reassign the A/B tokens to the individual bridges
  // before instantiating them

  // Bridge A
  std::cout << "-------------Bridge A Setup-------------" << std::endl;

  // Forward LK_TOKEN_A to Bridge A and instantiate the bridge
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", *token_a_))
    << "Failed to set environment variable LIVEKIT_TOKEN";
  auto bridge_a = std::make_shared<Ros2LiveKitBridge>(
    createBridgeOptions(
      graph_a.context(), "/bridge_a_node",
      {kBidirectionalTopic}, {kBidirectionalTopic}));

  std::cout << "------------Bridge A Created------------" << std::endl;

  // Bridge B
  std::cout << "-------------Bridge B Setup-------------" << std::endl;

  // Forward LK_TOKEN_B to Bridge B and instantiate the bridge
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", *token_b_))
    << "Failed to set environment variable LIVEKIT_TOKEN";
  auto bridge_b = std::make_shared<Ros2LiveKitBridge>(
    createBridgeOptions(
      graph_b.context(), "/bridge_b_node",
      {kBidirectionalTopic}, {kBidirectionalTopic}));
  std::cout << "------------Bridge B Created------------" << std::endl;

  // Each robot node is in the same ROS graph as its local bridge only.
  auto robot_a_node = std::make_shared<rclcpp::Node>(
    "participant_id_bridge_integration_robot_a",
    rclcpp::NodeOptions().context(graph_a.context()));
  auto robot_b_node = std::make_shared<rclcpp::Node>(
    "participant_id_bridge_integration_robot_b",
    rclcpp::NodeOptions().context(graph_b.context()));

  auto graph_a_executor_options = rclcpp::ExecutorOptions{};
  graph_a_executor_options.context = graph_a.context();
  rclcpp::executors::MultiThreadedExecutor graph_a_executor(
    graph_a_executor_options, 2);
  graph_a_executor.add_node(bridge_a);
  graph_a_executor.add_node(robot_a_node);

  auto graph_b_executor_options = rclcpp::ExecutorOptions{};
  graph_b_executor_options.context = graph_b.context();
  rclcpp::executors::MultiThreadedExecutor graph_b_executor(
    graph_b_executor_options, 2);
  graph_b_executor.add_node(bridge_b);
  graph_b_executor.add_node(robot_b_node);

  std::atomic_bool graph_a_spinning{true};
  std::atomic_bool graph_b_spinning{true};
  std::thread graph_a_spin_thread([&]() {
      graph_a_executor.spin();
      graph_a_spinning.store(false);
    });
  std::thread graph_b_spin_thread([&]() {
      graph_b_executor.spin();
      graph_b_spinning.store(false);
    });

  const auto stop_executors = [&]() {
      if (graph_a_spinning.exchange(false)) {
        graph_a_executor.cancel();
      }
      if (graph_b_spinning.exchange(false)) {
        graph_b_executor.cancel();
      }
      if (graph_a_spin_thread.joinable()) {
        graph_a_spin_thread.join();
      }
      if (graph_b_spin_thread.joinable()) {
        graph_b_spin_thread.join();
      }
    };

  auto publisher_a =
    robot_a_node->create_publisher<std_msgs::msg::String>(kBidirectionalTopic, 10);
  auto publisher_b =
    robot_b_node->create_publisher<std_msgs::msg::String>(kBidirectionalTopic, 10);

  ASSERT_TRUE(waitFor(
      [&]() {
        return topicExists(*robot_a_node, kBidirectionalTopic) &&
               topicExists(*robot_b_node, kBidirectionalTopic);
    },
    kGraphTimeout));

  const auto verify_direction =
    [&](const std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> &
    publisher,
    const std::shared_ptr<rclcpp::Node> & receiver_node,
    const std::string & source_topic,
    const std::string & expected_inbound_topic,
    const std::string & expected_payload) -> bool {
      if (!waitFor(
          [&]() {return publisher->get_subscription_count() > 0;},
        kGraphTimeout))
      {
        ADD_FAILURE() << "Bridge did not subscribe to " << source_topic;
        return false;
      }

      std::optional<std::string> inbound_topic;
      if (!waitFor(
          [&]() {
            publisher->publish(makeMessage("warmup:" + expected_payload));
            inbound_topic =
            findParticipantPrefixedTopic(*receiver_node, source_topic);
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

      auto subscription =
        receiver_node->create_subscription<std_msgs::msg::String>(
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
        if (cv.wait_for(lock, 100ms, [&]() {
            return received_payload.has_value();
        }))
        {
          break;
        }
      }

      subscription.reset();
      if (received_payload != expected_payload) {
        ADD_FAILURE() << "Did not receive payload on " << *inbound_topic;
        return false;
      }
      return true;
    };

  const bool a_to_b =
    verify_direction(
      publisher_a,
      robot_b_node,
      kBidirectionalTopic,
      expectedInboundTopicName(identity_a_, kBidirectionalTopic),
      "message from bridge a");
  const bool b_to_a =
    verify_direction(
      publisher_b,
      robot_a_node,
      kBidirectionalTopic,
      expectedInboundTopicName(identity_b_, kBidirectionalTopic),
      "message from bridge b");

  stop_executors();
  graph_b_executor.remove_node(robot_b_node);
  graph_b_executor.remove_node(bridge_b);
  graph_a_executor.remove_node(robot_a_node);
  graph_a_executor.remove_node(bridge_a);

  EXPECT_TRUE(a_to_b);
  EXPECT_TRUE(b_to_a);
}
