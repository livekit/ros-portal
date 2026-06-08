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

#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using ros2_livekit_bridge::Ros2LiveKitBridge;

constexpr auto kGraphTimeout = 15s;
constexpr auto kMessageTimeout = 20s;
constexpr auto kPollInterval = 50ms;

std::optional<std::string> getenvString(const char * name)
{
  const char * value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

bool setEnv(const char * name, const std::string & value)
{
  return ::setenv(name, value.c_str(), 1) == 0;
}

void restoreEnv(const char * name, const std::optional<std::string> & value)
{
  if (value) {
    (void)::setenv(name, value->c_str(), 1);
  } else {
    (void)::unsetenv(name);
  }
}

rclcpp::NodeOptions bridgeOptions(
  const std::string & node_namespace,
  const std::vector<std::string> & ros_topics)
{
  const std::vector<std::string> inbound_topics{
    "/bridge_a/out",
    "/bridge_b/out",
  };
  const std::vector<std::string> inbound_topic_types{
    "/bridge_a/out=std_msgs/msg/String",
    "/bridge_b/out=std_msgs/msg/String",
  };

  return rclcpp::NodeOptions()
         .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})
         .parameter_overrides({
      rclcpp::Parameter("room_name", "integration_test"),
      rclcpp::Parameter("topic_polling_period_ms", 50),
      rclcpp::Parameter("ros_threads", 4),
      rclcpp::Parameter("ros_topics", ros_topics),
      rclcpp::Parameter("livekit_to_ros_allow_topics", inbound_topics),
      rclcpp::Parameter("livekit_to_ros_topic_types", inbound_topic_types),
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

std_msgs::msg::String makeMessage(const std::string & data)
{
  std_msgs::msg::String msg;
  msg.data = data;
  return msg;
}

class ScopedRclcpp
{
public:
  ScopedRclcpp()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
      initialized_here_ = true;
    }
  }

  ~ScopedRclcpp()
  {
    if (initialized_here_ && rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }

private:
  bool initialized_here_{false};
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
};

}  // namespace

TEST_F(
  BridgeTestE2E,
  RepublishesRosMessagesBothWays)
{
  // Ensure environment variables are set
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

  ScopedRclcpp rclcpp_scope;
  ASSERT_TRUE(setEnv("LIVEKIT_URL", *livekit_url_))
    << "Failed to set environment variable LIVEKIT_URL";

  // Context: right now the bridge config only accepts a LIVEKIT_TOKEN env var, since only a single bridge is needed on a compute.
  // Instead of messing with that, reassign the A/B tokens to the individual bridges before instantiating them

  // Bridge A
  // Forward LK_TOKEN_A to Bridge A and instantiate the bridge
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", *token_a_))
    << "Failed to set environment variable LIVEKIT_TOKEN";
  auto bridge_a = std::make_shared<Ros2LiveKitBridge>(bridgeOptions("/bridge_a_node",
      {"/bridge_a/out"}));

  // Bridge B
  // Forward LK_TOKEN_B to Bridge B and instantiate the bridge
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", *token_b_))
    << "Failed to set environment variable LIVEKIT_TOKEN";
  auto bridge_b = std::make_shared<Ros2LiveKitBridge>(bridgeOptions("/bridge_b_node",
      {"/bridge_b/out"}));

  // Model two independent ROS participants instead of a shared harness node.
  auto robot_a_node = std::make_shared<rclcpp::Node>(
    "participant_id_bridge_integration_robot_a");
  auto robot_b_node = std::make_shared<rclcpp::Node>(
    "participant_id_bridge_integration_robot_b");

  rclcpp::executors::MultiThreadedExecutor executor(
    rclcpp::ExecutorOptions{}, 4);
  executor.add_node(bridge_a);
  executor.add_node(bridge_b);
  executor.add_node(robot_a_node);
  executor.add_node(robot_b_node);

  std::atomic_bool spinning{true};
  std::thread spin_thread([&]() {
      executor.spin();
      spinning.store(false);
    });

  const auto stop_executor = [&]() {
      if (spinning.exchange(false)) {
        executor.cancel();
      }
      if (spin_thread.joinable()) {
        spin_thread.join();
      }
    };

  auto publisher_a =
    robot_a_node->create_publisher<std_msgs::msg::String>("/bridge_a/out", 10);
  auto publisher_b =
    robot_b_node->create_publisher<std_msgs::msg::String>("/bridge_b/out", 10);

  const auto verify_direction =
    [&](const std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> &
    publisher,
    const std::shared_ptr<rclcpp::Node> & receiver_node,
    const std::string & source_topic,
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

      std::mutex mutex;
      std::condition_variable cv;
      std::optional<std::string> received_payload;

      auto subscription = receiver_node->create_subscription<std_msgs::msg::String>(
        *inbound_topic, 10,
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
    "/bridge_a/out",
    "message from bridge a");
  const bool b_to_a =
    verify_direction(
    publisher_b,
    robot_a_node,
    "/bridge_b/out",
    "message from bridge b");

  stop_executor();
  executor.remove_node(robot_b_node);
  executor.remove_node(robot_a_node);
  executor.remove_node(bridge_b);
  executor.remove_node(bridge_a);

  EXPECT_TRUE(a_to_b);
  EXPECT_TRUE(b_to_a);
}
