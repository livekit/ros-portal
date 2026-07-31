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

// End-to-end test for latched-topic forwarding (the /tf_static use case). Two
// ROS Portal nodes join one LiveKit room on isolated DDS domains. Graph A publishes a
// latched (TRANSIENT_LOCAL) message BEFORE ROS Portal B connects; the test asserts
// that after B joins, the state is republished on graph B via a TRANSIENT_LOCAL
// publisher and that a LATE subscriber on graph B still receives it. This covers
// all three requirements: delivery of pre-join state, a latched inbound
// publisher, and ROS Portal-to-ROS Portal arrival + forwarding.
//
// Requires a running LiveKit server and credentials; see
// .token_helpers/set_test_tokens.bash. The test skips when they are absent.

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <sstream>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <thread>

#include "ros_portal/ros_portal.hpp"
#include "ros_portal/utils/ros_utils.hpp"
#include "test_common.hpp"

namespace ros_portal::test {
namespace {

using namespace std::chrono_literals;

constexpr auto kGraphTimeout = 15s;
constexpr auto kDeliveryTimeout = 25s;
constexpr const char* kLatchedTopic = "/latched_state";
constexpr const char* kPayload = "latched-payload";

std::string latchedConfigYaml(const std::string& topic, const std::string& direction) {
  std::ostringstream stream;
  stream << "ros_portal:\n"
         << "  version: \"0.0.1\"\n"
         << "  topic_polling_period_ms: 50\n"
         << "  topics:\n"
         << "    - topic: \"" << topic << "\"\n"
         << "      direction: \"" << direction << "\"\n"
         << "      latched: true\n";
  return stream.str();
}

rclcpp::NodeOptions makeRosPortalOptions(const rclcpp::Context::SharedPtr& context, const std::string& node_namespace,
                                         const std::string& config_path) {
  return rclcpp::NodeOptions()
      .context(context)
      .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})
      .parameter_overrides({rclcpp::Parameter("config_path", config_path)});
}

rclcpp::QoS latchedQoS() { return rclcpp::QoS(rclcpp::KeepLast(10)).reliable().transient_local(); }

std_msgs::msg::String makeMessage(const std::string& data) {
  std_msgs::msg::String message;
  message.data = data;
  return message;
}

bool hasTransientLocalPublisher(rclcpp::Node& node, const std::string& topic) {
  for (const auto& info : node.get_publishers_info_by_topic(topic)) {
    if (info.qos_profile().durability() == rclcpp::DurabilityPolicy::TransientLocal) {
      return true;
    }
  }
  return false;
}

} // namespace

class LatchedTfStaticE2E : public ::testing::Test {
protected:
  static void SetUpTestSuite() { livekit::initialize(livekit::LogLevel::Info); }
  static void TearDownTestSuite() { livekit::shutdown(); }

  void SetUp() override {
    std::string source;
    url_ = utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    token_a_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_A", source);
    token_b_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_B", source);

    const auto original_token = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", source);
    original_token_ = original_token.empty() ? std::nullopt : std::optional<std::string>(original_token);
    const auto original_url = utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    original_url_ = original_url.empty() ? std::nullopt : std::optional<std::string>(original_url);
  }

  void TearDown() override {
    teardown();
    restoreEnv("LIVEKIT_TOKEN", original_token_);
    restoreEnv("LIVEKIT_URL", original_url_);
  }

  bool configured() const { return !url_.empty() && !token_a_.empty() && !token_b_.empty(); }

  std::shared_ptr<RosPortal> makeRosPortal(const rclcpp::Context::SharedPtr& context, const std::string& node_namespace,
                                           const std::string& token, const std::string& config_path) {
    if (!setEnv("LIVEKIT_TOKEN", token) || !setEnv("LIVEKIT_URL", url_)) {
      ADD_FAILURE() << "Failed to set LiveKit environment variables";
      return nullptr;
    }
    auto ros_portal = std::make_shared<RosPortal>(makeRosPortalOptions(context, node_namespace, config_path));
    if (!ros_portal->initialize()) {
      ADD_FAILURE() << "ROS Portal failed to initialize for " << node_namespace;
      return nullptr;
    }
    return ros_portal;
  }

  void spin(const std::unique_ptr<rclcpp::executors::MultiThreadedExecutor>& executor, std::thread& thread,
            std::atomic_bool& spinning) {
    spinning.store(true);
    thread = std::thread([&]() {
      executor->spin();
      spinning.store(false);
    });
  }

  void teardown() {
    if (executor_a_ && spinning_a_.exchange(false)) {
      executor_a_->cancel();
    }
    if (executor_b_ && spinning_b_.exchange(false)) {
      executor_b_->cancel();
    }
    if (thread_a_.joinable()) {
      thread_a_.join();
    }
    if (thread_b_.joinable()) {
      thread_b_.join();
    }
    if (executor_a_) {
      if (robot_a_node_) {
        executor_a_->remove_node(robot_a_node_);
      }
      if (ros_portal_a_) {
        executor_a_->remove_node(ros_portal_a_);
      }
    }
    if (executor_b_) {
      if (robot_b_node_) {
        executor_b_->remove_node(robot_b_node_);
      }
      if (ros_portal_b_) {
        executor_b_->remove_node(ros_portal_b_);
      }
    }
    if (ros_portal_a_) {
      ros_portal_a_->shutdown();
    }
    if (ros_portal_b_) {
      ros_portal_b_->shutdown();
    }
    publisher_a_.reset();
    ros_portal_a_.reset();
    ros_portal_b_.reset();
    robot_a_node_.reset();
    robot_b_node_.reset();
    executor_a_.reset();
    executor_b_.reset();
    graph_a_.reset();
    graph_b_.reset();
    config_a_.reset();
    config_b_.reset();
  }

  std::string url_;
  std::string token_a_;
  std::string token_b_;
  std::optional<std::string> original_token_;
  std::optional<std::string> original_url_;

  std::unique_ptr<ScopedRosGraph> graph_a_;
  std::unique_ptr<ScopedRosGraph> graph_b_;
  std::unique_ptr<TemporaryConfigFile> config_a_;
  std::unique_ptr<TemporaryConfigFile> config_b_;
  std::shared_ptr<RosPortal> ros_portal_a_;
  std::shared_ptr<RosPortal> ros_portal_b_;
  std::shared_ptr<rclcpp::Node> robot_a_node_;
  std::shared_ptr<rclcpp::Node> robot_b_node_;
  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> executor_a_;
  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> executor_b_;
  std::thread thread_a_;
  std::thread thread_b_;
  std::atomic_bool spinning_a_{false};
  std::atomic_bool spinning_b_{false};
  std::shared_ptr<rclcpp::Publisher<std_msgs::msg::String>> publisher_a_;
};

TEST_F(LatchedTfStaticE2E, RepublishesLatchedStatePublishedBeforePeerJoined) {
  if (!configured()) {
    GTEST_SKIP() << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set "
                    "(source .token_helpers/set_test_tokens.bash)";
  }

  const auto [domain_a, domain_b] = testDomainIds();
  ASSERT_NE(domain_a, domain_b);
  graph_a_ = std::make_unique<ScopedRosGraph>(domain_a);
  graph_b_ = std::make_unique<ScopedRosGraph>(domain_b);

  rclcpp::ExecutorOptions exec_options_a;
  exec_options_a.context = graph_a_->context();
  executor_a_ = std::make_unique<rclcpp::executors::MultiThreadedExecutor>(exec_options_a, 2);
  rclcpp::ExecutorOptions exec_options_b;
  exec_options_b.context = graph_b_->context();
  executor_b_ = std::make_unique<rclcpp::executors::MultiThreadedExecutor>(exec_options_b, 2);

  // --- Graph A: publish the latched state BEFORE ROS Portal B exists. ---
  robot_a_node_ = std::make_shared<rclcpp::Node>("latched_robot_a", rclcpp::NodeOptions().context(graph_a_->context()));
  publisher_a_ = robot_a_node_->create_publisher<std_msgs::msg::String>(kLatchedTopic, latchedQoS());
  publisher_a_->publish(makeMessage(kPayload)); // latched: retained for late subscribers, incl. ROS Portal

  config_a_ = std::make_unique<TemporaryConfigFile>(latchedConfigYaml(kLatchedTopic, "out"), "ros_portal_latched_a_");
  ros_portal_a_ = makeRosPortal(graph_a_->context(), "/ros_portal_latched_a", token_a_, config_a_->path().string());
  ASSERT_NE(ros_portal_a_, nullptr);

  executor_a_->add_node(ros_portal_a_);
  executor_a_->add_node(robot_a_node_);
  spin(executor_a_, thread_a_, spinning_a_);

  // ROS Portal A subscribes to the latched topic and, via TRANSIENT_LOCAL, captures
  // the sample published before it started.
  ASSERT_TRUE(waitFor([&]() { return publisher_a_->get_subscription_count() > 0; }, kGraphTimeout))
      << "ROS Portal A never subscribed to " << kLatchedTopic;

  // --- Graph B: bring up the consuming ROS Portal node only now (peer joins late). ---
  robot_b_node_ = std::make_shared<rclcpp::Node>("latched_robot_b", rclcpp::NodeOptions().context(graph_b_->context()));
  config_b_ = std::make_unique<TemporaryConfigFile>(latchedConfigYaml(kLatchedTopic, "in"), "ros_portal_latched_b_");
  ros_portal_b_ = makeRosPortal(graph_b_->context(), "/ros_portal_latched_b", token_b_, config_b_->path().string());
  ASSERT_NE(ros_portal_b_, nullptr);

  executor_b_->add_node(ros_portal_b_);
  executor_b_->add_node(robot_b_node_);
  spin(executor_b_, thread_b_, spinning_b_);

  // ROS Portal B must republish on a TRANSIENT_LOCAL publisher (Req 2).
  ASSERT_TRUE(waitFor([&]() { return hasTransientLocalPublisher(*robot_b_node_, kLatchedTopic); }, kDeliveryTimeout))
      << "ROS Portal B never created a TRANSIENT_LOCAL publisher for " << kLatchedTopic;

  // A LATE subscriber on graph B still receives the latched state (Req 1 + 3):
  // created after ROS Portal published, it relies on TRANSIENT_LOCAL replay.
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::string> received;
  auto subscription = robot_b_node_->create_subscription<std_msgs::msg::String>(
      kLatchedTopic, latchedQoS(), [&](const std_msgs::msg::String::ConstSharedPtr message) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          received = message->data;
        }
        cv.notify_all();
      });

  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(cv.wait_for(lock, kDeliveryTimeout, [&]() { return received.has_value(); }))
        << "Late subscriber on graph B did not receive the latched state";
  }
  subscription.reset();
  EXPECT_EQ(received.value_or(""), kPayload);
}

} // namespace ros_portal::test
