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
#include <livekit/room.h>

#include <chrono>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <future>
#include <memory>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <string>
#include <thread>

#include "ros_portal/connection/connection_manager.hpp"
#include "ros_portal/ros_portal.hpp"

namespace ros_portal {

namespace {

using namespace std::chrono_literals;
using Trigger = std_srvs::srv::Trigger;

std::optional<std::string> diagnosticValueFor(const diagnostic_updater::DiagnosticStatusWrapper& status,
                                              const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

class ScopedExecutorSpin {
public:
  explicit ScopedExecutorSpin(rclcpp::executors::SingleThreadedExecutor& executor)
      : executor_(executor), spin_thread_([&executor]() { executor.spin(); }) {}

  ~ScopedExecutorSpin() {
    executor_.cancel();
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
  }

  ScopedExecutorSpin(const ScopedExecutorSpin&) = delete;
  ScopedExecutorSpin& operator=(const ScopedExecutorSpin&) = delete;

private:
  rclcpp::executors::SingleThreadedExecutor& executor_;
  std::thread spin_thread_;
};

Trigger::Response::SharedPtr callTrigger(rclcpp::Node& node, const std::string& service_name) {
  auto client = node.create_client<Trigger>(service_name);
  if (!client->wait_for_service(2s)) {
    return nullptr;
  }

  auto future = client->async_send_request(std::make_shared<Trigger::Request>());
  if (future.wait_for(2s) != std::future_status::ready) {
    return nullptr;
  }
  return future.get();
}

/// @brief Invoke a `~/pause` or `~/resume` handler directly on the node.
Trigger::Response::SharedPtr callHandler(void (RosPortal::*handler)(const std::shared_ptr<Trigger::Request>&,
                                                                    const std::shared_ptr<Trigger::Response>&),
                                         RosPortal& portal) {
  auto response = std::make_shared<Trigger::Response>();
  (portal.*handler)(std::make_shared<Trigger::Request>(), response);
  return response;
}

} // namespace

TEST(RosPortalPauseResumeTest, AdvertisesPauseAndResumeServices) {
  auto portal = std::make_shared<RosPortal>();
  auto client_node = std::make_shared<rclcpp::Node>("ros_portal_operation_services_client_node");

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(portal);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  // ROS Portal is not initialized here, so both services must answer rather
  // than time out, and must report the failure honestly.
  const auto pause_response = callTrigger(*client_node, "/ros_portal/pause");
  ASSERT_NE(pause_response, nullptr);
  EXPECT_FALSE(pause_response->success);
  EXPECT_EQ(pause_response->message, "ROS Portal is not initialized");

  const auto resume_response = callTrigger(*client_node, "/ros_portal/resume");
  ASSERT_NE(resume_response, nullptr);
  EXPECT_FALSE(resume_response->success);
  EXPECT_EQ(resume_response->message, "ROS Portal is not initialized");
}

TEST(RosPortalPauseResumeTest, ReportsNotInitializedWithoutConnectionManager) {
  RosPortal portal;
  ASSERT_EQ(portal.connection_manager_, nullptr);

  const auto resume_response = callHandler(&RosPortal::handleResumeForwardingRequest, portal);
  EXPECT_FALSE(resume_response->success);
  EXPECT_EQ(resume_response->message, "ROS Portal is not initialized");

  const auto pause_response = callHandler(&RosPortal::handlePauseForwardingRequest, portal);
  EXPECT_FALSE(pause_response->success);
  EXPECT_EQ(pause_response->message, "ROS Portal is not initialized");
}

TEST(RosPortalPauseResumeTest, PauseAndResumeForwardingAreIdempotent) {
  RosPortal portal;
  livekit::Room room;

  portal.connection_manager_ =
      std::make_unique<ConnectionManager>(ConnectionManager::Methods{[]() { return true; }},
                                          portal.get_logger().get_child("connection"), portal.makeDiagnosticsFns());
  portal.room_operations_enabled_ = portal.connection_manager_->operationsEnabledFlag();
  portal.connection_manager_->poll(room);
  ASSERT_TRUE(portal.roomOperationsEnabled());

  const auto paused = callHandler(&RosPortal::handlePauseForwardingRequest, portal);
  EXPECT_TRUE(paused->success);
  EXPECT_EQ(paused->message, "ROS Portal paused");
  EXPECT_FALSE(portal.roomOperationsEnabled());
  EXPECT_TRUE(portal.isForwardingPaused());

  const auto paused_again = callHandler(&RosPortal::handlePauseForwardingRequest, portal);
  EXPECT_TRUE(paused_again->success);
  EXPECT_EQ(paused_again->message, "ROS Portal is already paused");
  EXPECT_FALSE(portal.roomOperationsEnabled());

  const auto resumed = callHandler(&RosPortal::handleResumeForwardingRequest, portal);
  EXPECT_TRUE(resumed->success);
  EXPECT_EQ(resumed->message, "ROS Portal resumed");
  EXPECT_TRUE(portal.roomOperationsEnabled());
  EXPECT_FALSE(portal.isForwardingPaused());

  const auto resumed_again = callHandler(&RosPortal::handleResumeForwardingRequest, portal);
  EXPECT_TRUE(resumed_again->success);
  EXPECT_EQ(resumed_again->message, "ROS Portal is already running");
  EXPECT_TRUE(portal.roomOperationsEnabled());
}

TEST(RosPortalPauseResumeTest, ResumeWithoutRoomConnectionDefersOperations) {
  RosPortal portal;

  portal.connection_manager_ =
      std::make_unique<ConnectionManager>(ConnectionManager::Methods{[]() { return false; }},
                                          portal.get_logger().get_child("connection"), portal.makeDiagnosticsFns());
  portal.room_operations_enabled_ = portal.connection_manager_->operationsEnabledFlag();
  ASSERT_FALSE(portal.roomOperationsEnabled());

  // Never paused but never connected either: the portal is running, so a
  // resume request succeeds without claiming that operations are enabled.
  const auto running = callHandler(&RosPortal::handleResumeForwardingRequest, portal);
  EXPECT_TRUE(running->success);
  EXPECT_EQ(running->message, "ROS Portal is already running; waiting for a room connection");
  EXPECT_FALSE(portal.isForwardingPaused());

  const auto paused = callHandler(&RosPortal::handlePauseForwardingRequest, portal);
  EXPECT_TRUE(paused->success);
  EXPECT_EQ(paused->message, "ROS Portal paused");
  EXPECT_TRUE(portal.isForwardingPaused());

  const auto resumed = callHandler(&RosPortal::handleResumeForwardingRequest, portal);
  EXPECT_TRUE(resumed->success);
  EXPECT_EQ(resumed->message, "ROS Portal resumed; waiting for a room connection");
  EXPECT_FALSE(portal.isForwardingPaused());
  EXPECT_FALSE(portal.roomOperationsEnabled());
}

TEST(RosPortalPauseResumeTest, ReportsPausedOperationsInDiagnostics) {
  RosPortal portal;
  livekit::Room room;

  portal.connection_manager_ =
      std::make_unique<ConnectionManager>(ConnectionManager::Methods{[]() { return true; }},
                                          portal.get_logger().get_child("connection"), portal.makeDiagnosticsFns());
  portal.room_operations_enabled_ = portal.connection_manager_->operationsEnabledFlag();
  portal.connection_manager_->poll(room);
  portal.initialized_.store(true);
  portal.diagnostic_state_.graph_discovery_active.store(true);
  portal.diagnostic_state_.connection_manager_active.store(true);
  portal.diagnostic_state_.topic_forwarder_active.store(true);
  portal.diagnostic_state_.latched_topic_forwarder_active.emplace();
  portal.diagnostic_state_.latched_topic_forwarder_active->store(true);
  portal.diagnostic_state_.service_forwarder_active.store(true);
  portal.diagnostic_state_.cli_manager_active.store(true);

  diagnostic_updater::DiagnosticStatusWrapper running_status;
  portal.populateStatus(running_status);
  EXPECT_EQ(running_status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(diagnosticValueFor(running_status, "forwarding_paused"), "false");

  ASSERT_TRUE(callHandler(&RosPortal::handlePauseForwardingRequest, portal)->success);

  diagnostic_updater::DiagnosticStatusWrapper paused_status;
  portal.populateStatus(paused_status);
  EXPECT_EQ(paused_status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(paused_status.message, "ROS Portal operations are paused");
  EXPECT_EQ(diagnosticValueFor(paused_status, "forwarding_paused"), "true");
}

} // namespace ros_portal
