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
#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace ros2_livekit_bridge::ros2_cli
{
namespace
{

using namespace std::chrono_literals;

/// @brief Wait until a service appears in the caller node graph.
bool waitForService(
  rclcpp::Node & node,
  const std::string & service_name,
  std::chrono::milliseconds timeout = 2s)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (node.get_service_names_and_types().count(service_name) > 0U) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return false;
}

/// @brief Construct a ServiceCaller from a node.
ServiceCaller makeCaller(const std::shared_ptr<rclcpp::Node> & node)
{
  return ServiceCaller(
    node->get_node_base_interface(),
    node->get_node_graph_interface());
}

/// @brief Construct service-call options for SetBool.
ServiceCallOptions makeSetBoolOptions(
  std::string service = "/service_call/set_bool",
  std::string interface_type = "std_srvs/srv/SetBool",
  std::string payload = "{data: true}",
  std::uint8_t timeout_sec = 2)
{
  ServiceCallOptions options;
  options.service = std::move(service);
  options.interface_type = std::move(interface_type);
  std::string error;
  auto serialized = serializedMessageFromYaml(
    options.interface_type + "_Request", payload, error);
  if (serialized) {
    const auto & raw = serialized->get_rcl_serialized_message();
    if (raw.buffer != nullptr && serialized->size() > 0U) {
      options.request_payload.assign(
        raw.buffer, raw.buffer + serialized->size());
    }
  }
  options.timeout_sec = timeout_sec;
  return options;
}

class ServiceCallerTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    int argc = 0;
    char ** argv = nullptr;
    rclcpp::init(argc, argv);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }
};

TEST_F(ServiceCallerTest, CallsServiceAndReturnsYamlAndCdrResponse)
{
  auto caller_node = std::make_shared<rclcpp::Node>("service_call_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("service_call_server_node");

  auto service = server_node->create_service<std_srvs::srv::SetBool>(
    "/service_call/set_bool",
    [](const std_srvs::srv::SetBool::Request::SharedPtr request,
    std_srvs::srv::SetBool::Response::SharedPtr response) {
      response->success = request->data;
      response->message = request->data ? "enabled" : "disabled";
    });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  std::thread spin_thread([&executor]() {executor.spin();});

  ASSERT_TRUE(waitForService(*caller_node, "/service_call/set_bool"));

  auto caller = makeCaller(caller_node);
  const auto response = caller.call(makeSetBoolOptions());

  executor.cancel();
  spin_thread.join();

  ASSERT_TRUE(response.success) << response.err_msg;
  EXPECT_NE(response.output.find("success: true"), std::string::npos);
  EXPECT_NE(response.output.find("message: enabled"), std::string::npos);
}

TEST_F(ServiceCallerTest, RejectsEmptyInterfaceType)
{
  auto caller_node = std::make_shared<rclcpp::Node>(
    "service_call_empty_interface_type_node");
  auto caller = makeCaller(caller_node);

  ServiceCallOptions options;
  options.service = "/service_call/missing";
  options.timeout_sec = 1;
  options.request_payload = {0x01, 0x02, 0x03};
  const auto response = caller.call(options);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "interface_type must be non-empty");
}

TEST_F(ServiceCallerTest, HandlesUnavailableServiceTimeout)
{
  auto caller_node =
    std::make_shared<rclcpp::Node>("service_call_timeout_node");
  auto caller = makeCaller(caller_node);

  const auto response = caller.call(
    makeSetBoolOptions(
      "/service_call/unavailable", "std_srvs/srv/SetBool", "{data: true}", 1));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "Service call timed out.");
}

TEST_F(ServiceCallerTest, DropsLateTimedOutResponseBeforeLaterCall)
{
  auto caller_node =
    std::make_shared<rclcpp::Node>("service_call_late_caller_node");
  auto server_node =
    std::make_shared<rclcpp::Node>("service_call_late_server_node");

  auto service = server_node->create_service<std_srvs::srv::SetBool>(
    "/service_call/late_response",
    [](const std_srvs::srv::SetBool::Request::SharedPtr request,
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
  std::thread spin_thread([&executor]() {executor.spin();});

  ASSERT_TRUE(waitForService(*caller_node, "/service_call/late_response"));

  auto caller = makeCaller(caller_node);
  const auto timed_out = caller.call(
    makeSetBoolOptions(
      "/service_call/late_response", "std_srvs/srv/SetBool",
      "{data: true}", 1));
  EXPECT_FALSE(timed_out.success);
  EXPECT_EQ(timed_out.err_msg, "Service call timed out.");

  const auto response = caller.call(
    makeSetBoolOptions(
      "/service_call/late_response", "std_srvs/srv/SetBool",
      "{data: false}", 3));

  executor.cancel();
  spin_thread.join();

  ASSERT_TRUE(response.success) << response.err_msg;
  EXPECT_NE(response.output.find("message: current"), std::string::npos);
}

TEST_F(ServiceCallerTest, RejectsEmptyRequestPayload)
{
  auto caller_node =
    std::make_shared<rclcpp::Node>("service_call_empty_payload_node");
  auto caller = makeCaller(caller_node);

  ServiceCallOptions options;
  options.service = "/service_call/empty_payload";
  options.interface_type = "std_srvs/srv/SetBool";
  options.timeout_sec = 1;
  const auto response = caller.call(options);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "request payload must be non-empty");
}

}  // namespace
}  // namespace ros2_livekit_bridge::ros2_cli
