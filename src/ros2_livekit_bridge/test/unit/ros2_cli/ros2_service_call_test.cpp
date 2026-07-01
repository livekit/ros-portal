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

#include "ros2_livekit_bridge/ros2_cli/ros2_service_call.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <nav_msgs/srv/get_plan.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"

namespace ros2_livekit_bridge::ros2_cli {

namespace {

using namespace std::chrono_literals;

/// @brief Wait until a service appears in the caller node graph.
bool waitForService(rclcpp::Node &node, const std::string &service_name, std::chrono::milliseconds timeout = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (node.get_service_names_and_types().count(service_name) > 0U) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

/// @brief RAII guard that spins an executor on a background thread and
/// guarantees it is cancelled and joined on scope exit. This keeps the test
/// process from terminating on a joinable thread when an ASSERT_* macro
/// triggers an early return before manual cleanup would run.
class ScopedExecutorSpin {
public:
  explicit ScopedExecutorSpin(rclcpp::executors::SingleThreadedExecutor &executor)
      : executor_(executor), spin_thread_([&executor]() { executor.spin(); }) {}

  ~ScopedExecutorSpin() {
    executor_.cancel();
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
  }

  ScopedExecutorSpin(const ScopedExecutorSpin &) = delete;
  ScopedExecutorSpin &operator=(const ScopedExecutorSpin &) = delete;
  ScopedExecutorSpin(ScopedExecutorSpin &&) = delete;
  ScopedExecutorSpin &operator=(ScopedExecutorSpin &&) = delete;

private:
  rclcpp::executors::SingleThreadedExecutor &executor_;
  std::thread spin_thread_;
};

/// @brief Construct a Ros2ServiceCall from a node.
Ros2ServiceCall makeCaller(const std::shared_ptr<rclcpp::Node> &node) {
  return Ros2ServiceCall(node->get_node_base_interface(), node->get_node_graph_interface(), node->get_logger());
}

/// @brief Construct service-call options for SetBool.
ServiceCallOptions makeSetBoolOptions(std::string service = "/service_call/set_bool",
                                      std::string msg_type = "std_srvs/srv/SetBool",
                                      std::string payload = "{data: true}", std::uint8_t timeout_sec = 2) {
  ServiceCallOptions options;
  options.service = std::move(service);
  options.msg_type = std::move(msg_type);
  options.payload = std::move(payload);
  options.timeout_sec = timeout_sec;
  return options;
}

class Ros2ServiceCallTest : public ::testing::Test {};

TEST_F(Ros2ServiceCallTest, CallsServiceAndReturnsYamlAndCdrResponse) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("service_call_server_node");

  auto service = server_node->create_service<std_srvs::srv::SetBool>(
      "/service_call/set_bool", [](const std_srvs::srv::SetBool::Request::SharedPtr request,
                                   std_srvs::srv::SetBool::Response::SharedPtr response) {
        response->success = request->data;
        response->message = request->data ? "enabled" : "disabled";
      });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*caller_node, "/service_call/set_bool"));

  auto caller = makeCaller(caller_node);
  const auto response = caller.call(makeSetBoolOptions());

  ASSERT_TRUE(response.success) << response.err_msg;
  EXPECT_NE(response.output.find("success: true"), std::string::npos);
  EXPECT_NE(response.output.find("message: enabled"), std::string::npos);
}

TEST_F(Ros2ServiceCallTest, RendersNestedResponseFieldsWithAccumulatingIndent) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_nested_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("service_call_nested_server_node");

  // GetPlan's response is a single nav_msgs/Path plan, which nests
  // plan -> header -> stamp, exercising three levels of YAML indentation.
  auto service = server_node->create_service<nav_msgs::srv::GetPlan>(
      "/service_call/get_plan", [](const nav_msgs::srv::GetPlan::Request::SharedPtr request,
                                   nav_msgs::srv::GetPlan::Response::SharedPtr response) {
        (void)request;
        response->plan.header.frame_id = "map";
        response->plan.header.stamp.sec = 7;
        response->plan.header.stamp.nanosec = 0U;
      });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*caller_node, "/service_call/get_plan"));

  auto caller = makeCaller(caller_node);
  const auto response = caller.call(makeSetBoolOptions("/service_call/get_plan", "nav_msgs/srv/GetPlan", "{}", 2));

  ASSERT_TRUE(response.success) << response.err_msg;
  // Indentation must accumulate two spaces per nesting level relative to the
  // parent, rather than resetting to a fixed depth:
  //   plan:           (0)
  //     header:       (2)
  //       stamp:      (4)
  //         sec: 7    (6)
  //         nanosec: 0(6)
  //       frame_id: map (4)
  //     poses: []     (2)
  EXPECT_NE(response.output.find("\n  header: \n"), std::string::npos) << response.output;
  EXPECT_NE(response.output.find("\n    stamp: \n"), std::string::npos) << response.output;
  EXPECT_NE(response.output.find("\n      sec: 7\n"), std::string::npos) << response.output;
  EXPECT_NE(response.output.find("\n      nanosec: 0\n"), std::string::npos) << response.output;
  EXPECT_NE(response.output.find("\n    frame_id: map\n"), std::string::npos) << response.output;
  EXPECT_NE(response.output.find("\n  poses: ["), std::string::npos) << response.output;
}

TEST_F(Ros2ServiceCallTest, RejectsEmptyInterfaceType) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_empty_msg_type_node");
  auto caller = makeCaller(caller_node);

  ServiceCallOptions options;
  options.service = "/service_call/missing";
  options.timeout_sec = 1;
  options.payload = "{data: true}";
  const auto response = caller.call(options);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "msg_type must be non-empty");
}

TEST_F(Ros2ServiceCallTest, HandlesUnavailableServiceTimeout) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_timeout_node");
  auto caller = makeCaller(caller_node);

  const auto response =
      caller.call(makeSetBoolOptions("/service_call/unavailable", "std_srvs/srv/SetBool", "{data: true}", 1));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "Service call timed out.");
}

TEST_F(Ros2ServiceCallTest, DropsLateTimedOutResponseBeforeLaterCall) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_late_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("service_call_late_server_node");

  auto service = server_node->create_service<std_srvs::srv::SetBool>(
      "/service_call/late_response", [](const std_srvs::srv::SetBool::Request::SharedPtr request,
                                        std_srvs::srv::SetBool::Response::SharedPtr response) {
        if (request->data) {
          std::this_thread::sleep_for(1500ms);
        }
        response->success = request->data;
        response->message = request->data ? "late" : "current";
      });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*caller_node, "/service_call/late_response"));

  auto caller = makeCaller(caller_node);
  const auto timed_out =
      caller.call(makeSetBoolOptions("/service_call/late_response", "std_srvs/srv/SetBool", "{data: true}", 1));
  EXPECT_FALSE(timed_out.success);
  EXPECT_EQ(timed_out.err_msg, "Service call timed out.");

  const auto response =
      caller.call(makeSetBoolOptions("/service_call/late_response", "std_srvs/srv/SetBool", "{data: false}", 3));

  ASSERT_TRUE(response.success) << response.err_msg;
  EXPECT_NE(response.output.find("message: current"), std::string::npos);
}

TEST_F(Ros2ServiceCallTest, RejectsEmptyRequestPayload) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_empty_payload_node");
  auto caller = makeCaller(caller_node);

  ServiceCallOptions options;
  options.service = "/service_call/empty_payload";
  options.msg_type = "std_srvs/srv/SetBool";
  options.timeout_sec = 1;
  const auto response = caller.call(options);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "failed to build service request: payload must be non-empty");
}

TEST_F(Ros2ServiceCallTest, RejectsUnknownServiceTypeWithoutThrowing) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_unknown_type_node");
  auto caller = makeCaller(caller_node);

  const auto response =
      caller.call(makeSetBoolOptions("/service_call/unknown_type", "fake_msgs/srv/DoesNotExist", "{data: true}", 1));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("failed to build service request:"), std::string::npos);
}

TEST_F(Ros2ServiceCallTest, RejectsServiceClientCacheLimit) {
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_cache_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("service_call_cache_server_node");
  std::vector<rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr> services;
  services.reserve(kMaxCachedServiceClients);

  for (std::size_t index = 0; index < kMaxCachedServiceClients; ++index) {
    const std::string service_name = "/service_call/cache_fill/svc_" + std::to_string(index);
    services.push_back(server_node->create_service<std_srvs::srv::SetBool>(
        service_name, [](const std_srvs::srv::SetBool::Request::SharedPtr request,
                         std_srvs::srv::SetBool::Response::SharedPtr response) {
          response->success = true;
          response->message = request->data ? "enabled" : "disabled";
        }));
  }

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);

  auto caller = makeCaller(caller_node);
  for (std::size_t index = 0; index < kMaxCachedServiceClients; ++index) {
    const auto response = caller.call(makeSetBoolOptions("/service_call/cache_fill/svc_" + std::to_string(index),
                                                         "std_srvs/srv/SetBool", "{data: false}", 2));
    ASSERT_TRUE(response.success) << response.err_msg;
  }

  const auto overflow =
      caller.call(makeSetBoolOptions("/service_call/cache_overflow", "std_srvs/srv/SetBool", "{data: false}", 1));
  EXPECT_FALSE(overflow.success);
  EXPECT_EQ(overflow.err_msg, "failed to create service client: service client cache limit reached");
}

} // namespace
} // namespace ros2_livekit_bridge::ros2_cli
