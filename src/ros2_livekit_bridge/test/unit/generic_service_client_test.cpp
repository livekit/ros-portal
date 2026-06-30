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

#include "ros2_livekit_bridge/generic_service_client.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <string>
#include <thread>
#include <vector>

#include "ros2_livekit_bridge/service_type_support.hpp"

namespace ros2_livekit_bridge {
namespace {

using namespace std::chrono_literals;

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

std::vector<std::uint8_t> serializeSetBoolRequest(bool data) {
  std_srvs::srv::SetBool::Request request;
  request.data = data;
  rclcpp::Serialization<std_srvs::srv::SetBool::Request> serializer;
  rclcpp::SerializedMessage message;
  serializer.serialize_message(&request, &message);
  const auto &rcl = message.get_rcl_serialized_message();
  return std::vector<std::uint8_t>(rcl.buffer, rcl.buffer + rcl.buffer_length);
}

std_srvs::srv::SetBool::Response deserializeSetBoolResponse(const std::vector<std::uint8_t> &cdr) {
  rclcpp::SerializedMessage message(cdr.size());
  auto &rcl = message.get_rcl_serialized_message();
  std::memcpy(rcl.buffer, cdr.data(), cdr.size());
  rcl.buffer_length = cdr.size();
  rclcpp::Serialization<std_srvs::srv::SetBool::Response> serializer;
  std_srvs::srv::SetBool::Response response;
  serializer.deserialize_message(&message, &response);
  return response;
}

TEST(GenericServiceClientTest, CallsServiceWithCdrRoundTrip) {
  auto caller_node = std::make_shared<rclcpp::Node>("gsc_caller_node");
  auto server_node = std::make_shared<rclcpp::Node>("gsc_server_node");

  auto service = server_node->create_service<std_srvs::srv::SetBool>(
      "/gsc/set_bool", [](const std_srvs::srv::SetBool::Request::SharedPtr request,
                          std_srvs::srv::SetBool::Response::SharedPtr response) {
        response->success = request->data;
        response->message = request->data ? "enabled" : "disabled";
      });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(caller_node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*caller_node, "/gsc/set_bool"));

  std::string error;
  auto support = ServiceTypeSupport::create("std_srvs/srv/SetBool", error);
  ASSERT_TRUE(support) << error;

  GenericServiceClient client("/gsc/set_bool", support, caller_node->get_node_base_interface().get(),
                              caller_node->get_node_graph_interface(), caller_node->get_logger());

  const auto response_cdr = client.call(serializeSetBoolRequest(true), 2s);
  ASSERT_TRUE(response_cdr.has_value());
  const auto response = deserializeSetBoolResponse(*response_cdr);
  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.message, "enabled");
}

TEST(GenericServiceClientTest, TimesOutWhenServiceAbsent) {
  auto caller_node = std::make_shared<rclcpp::Node>("gsc_timeout_node");

  std::string error;
  auto support = ServiceTypeSupport::create("std_srvs/srv/SetBool", error);
  ASSERT_TRUE(support) << error;

  GenericServiceClient client("/gsc/absent", support, caller_node->get_node_base_interface().get(),
                              caller_node->get_node_graph_interface(), caller_node->get_logger());

  const auto response_cdr = client.call(serializeSetBoolRequest(true), 300ms);
  EXPECT_FALSE(response_cdr.has_value());
}

} // namespace
} // namespace ros2_livekit_bridge
