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

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_topic_pub.hpp"
#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>

namespace ros2_livekit_bridge
{
namespace
{

TopicPubOptions makePublishOptions(
  std::string topic,
  std::string msg_type = "std_msgs/msg/String",
  std::string payload = "{data: hello}")
{
  TopicPubOptions options;
  options.topic = std::move(topic);
  options.msg_type = std::move(msg_type);
  options.payload = std::move(payload);
  return options;
}

class Ros2TopicPubTest : public ::testing::Test {
protected:
  static void SetUpTestSuite()
  {
    int argc = 0;
    char **argv = nullptr;
    rclcpp::init(argc, argv);
  }

  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("ros2_topic_pub_unit_test");
  }

  void TearDown() override {node_.reset();}

  ros2_cli::Ros2TopicPub
  makePublisher(ros2_cli::TopicPublishAllowed allowed = {}) const
  {
    return ros2_cli::Ros2TopicPub(node_->get_node_topics_interface(),
                                  node_->get_node_graph_interface(),
                                  std::move(allowed));
  }

  bool spinUntil(
    const std::function<bool()> & predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(2))
  {
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    executor.spin_some();
    return predicate();
  }

  std::shared_ptr<rclcpp::Node> node_;
};

TEST_F(Ros2TopicPubTest, PublishesYamlPayloadToTypedSubscriber) {
  const std::string topic = "/topic_pub/string_cmd";
  std::optional<std::string> received;
  const auto subscription = node_->create_subscription<std_msgs::msg::String>(
      topic, rclcpp::QoS(10),
    [&received](const std_msgs::msg::String & message) {
      received = message.data;
      });
  (void)subscription;

  auto publisher = makePublisher();
  auto response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/String", "{data: first}"));
  EXPECT_TRUE(response.success) << response.err_msg;
  ASSERT_TRUE(
      spinUntil([&]() {return node_->count_publishers(topic) == 1U;}));

  received.reset();
  response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/String", "{data: second}"));
  EXPECT_TRUE(response.success) << response.err_msg;

  ASSERT_TRUE(spinUntil([&]() {return received.has_value();}));
  EXPECT_EQ(*received, "second");
}

TEST_F(Ros2TopicPubTest, ResolvesRelativeTopicNames) {
  node_.reset();
  node_ = std::make_shared<rclcpp::Node>("ros2_topic_pub_namespaced_unit_test",
                                         "/robot");

  const std::string request_topic = "cmd/string";
  const std::string resolved_topic =
    node_->get_node_topics_interface()->resolve_topic_name(request_topic);
  std::optional<std::string> received;
  const auto subscription = node_->create_subscription<std_msgs::msg::String>(
      resolved_topic, rclcpp::QoS(10),
    [&received](const std_msgs::msg::String & message) {
      received = message.data;
      });
  (void)subscription;

  auto publisher = makePublisher();
  auto response = publisher.publish(makePublishOptions(
      request_topic, "std_msgs/msg/String", "{data: first}"));
  EXPECT_TRUE(response.success) << response.err_msg;
  ASSERT_TRUE(spinUntil(
      [&]() {return node_->count_publishers(resolved_topic) == 1U;}));

  received.reset();
  response = publisher.publish(makePublishOptions(
      request_topic, "std_msgs/msg/String", "{data: second}"));
  EXPECT_TRUE(response.success) << response.err_msg;

  ASSERT_TRUE(spinUntil([&]() {return received.has_value();}));
  EXPECT_EQ(*received, "second");
}

TEST_F(Ros2TopicPubTest, RejectsDeniedTopicBeforeCreatingPublisher) {
  const std::string topic = "/topic_pub/denied";
  auto publisher = makePublisher([](const std::string &) {return false;});

  const auto response = publisher.publish(makePublishOptions(topic));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("not allowed"), std::string::npos);
  EXPECT_FALSE(spinUntil([&]() {return node_->count_publishers(topic) != 0U;},
                         std::chrono::milliseconds(200)));
}

TEST_F(Ros2TopicPubTest, RejectsGraphTypeMismatch) {
  const std::string topic = "/topic_pub/type_mismatch";
  const auto subscription = node_->create_subscription<std_msgs::msg::String>(
      topic, rclcpp::QoS(10), [](const std_msgs::msg::String &) {});
  (void)subscription;
  ASSERT_TRUE(spinUntil([&]() {
      const auto topics = node_->get_topic_names_and_types();
      const auto found = topics.find(topic);
      return found != topics.end() &&
             ros2_cli::topicTypeMatches(found->second, "std_msgs/msg/String");
  }));

  auto publisher = makePublisher();
  const auto response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/Bool", "{data: true}"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("std_msgs/msg/String"), std::string::npos);
  EXPECT_FALSE(spinUntil([&]() {return node_->count_publishers(topic) != 0U;},
                         std::chrono::milliseconds(200)));
}

TEST_F(Ros2TopicPubTest, PinsCachedPublisherType) {
  const std::string topic = "/topic_pub/cached_type";
  auto publisher = makePublisher();

  auto response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/String", "{data: first}"));
  EXPECT_TRUE(response.success) << response.err_msg;
  ASSERT_TRUE(
      spinUntil([&]() {return node_->count_publishers(topic) == 1U;}));

  response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/Bool", "{data: true}"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("cached with type"), std::string::npos);
}

TEST_F(Ros2TopicPubTest, InvalidYamlPayloadFailsWithoutThrowing) {
  const std::string topic = "/topic_pub/invalid_yaml";
  auto publisher = makePublisher();

  const auto response = publisher.publish(
      makePublishOptions(topic, "std_msgs/msg/String", "{data: [}"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("failed to publish message"),
            std::string::npos);
}

TEST_F(Ros2TopicPubTest, EnforcesPublisherCacheLimit) {
  auto publisher = makePublisher();

  for (std::size_t i = 0; i < ros2_cli::kMaxCachedTopicPublishers; ++i) {
    const auto response = publisher.publish(
        makePublishOptions("/topic_pub/cache_" + std::to_string(i),
                           "std_msgs/msg/String", "{data: value}"));
    ASSERT_TRUE(response.success) << response.err_msg;
  }

  const auto response = publisher.publish(makePublishOptions(
      "/topic_pub/cache_overflow", "std_msgs/msg/String", "{data: value}"));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "topic publisher cache limit reached");
}

TEST(Ros2TopicPubStandaloneTest, ConstructorRequiresNodeInterfaces) {
  EXPECT_THROW(ros2_cli::Ros2TopicPub({}, {}), std::invalid_argument);
}

} // namespace
} // namespace ros2_livekit_bridge
