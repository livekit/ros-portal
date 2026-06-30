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

#include "ros2_livekit_bridge/ros2_cli/generic_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <thread>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/service_type_support.hpp"

namespace ros2_livekit_bridge::ros2_cli {
namespace {

using namespace std::chrono_literals;

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

// SetBool server callback: deserialize the request, echo `data` into success
// and produce a recognizable message, then serialize the response back to CDR.
std::optional<std::vector<std::uint8_t>> setBoolEchoCallback(std::vector<std::uint8_t> request_cdr) {
  rclcpp::SerializedMessage request_message(request_cdr.size());
  auto &rcl_request = request_message.get_rcl_serialized_message();
  std::memcpy(rcl_request.buffer, request_cdr.data(), request_cdr.size());
  rcl_request.buffer_length = request_cdr.size();
  rclcpp::Serialization<std_srvs::srv::SetBool::Request> request_serializer;
  std_srvs::srv::SetBool::Request request;
  request_serializer.deserialize_message(&request_message, &request);

  std_srvs::srv::SetBool::Response response;
  response.success = request.data;
  response.message = request.data ? "served-on" : "served-off";

  rclcpp::Serialization<std_srvs::srv::SetBool::Response> response_serializer;
  rclcpp::SerializedMessage response_message;
  response_serializer.serialize_message(&response, &response_message);
  const auto &rcl_response = response_message.get_rcl_serialized_message();
  return std::vector<std::uint8_t>(rcl_response.buffer, rcl_response.buffer + rcl_response.buffer_length);
}

std::shared_ptr<GenericService> makeServer(rclcpp::Node &node, rclcpp::CallbackGroup::SharedPtr callback_group,
                                           const std::string &service_name, GenericService::RequestCallback callback) {
  std::string error;
  auto support = ServiceTypeSupport::create("std_srvs/srv/SetBool", error);
  EXPECT_TRUE(support) << error;
  auto server = std::make_shared<GenericService>(node.get_node_base_interface()->get_shared_rcl_node_handle(),
                                                 service_name, support, std::move(callback));
  node.get_node_services_interface()->add_service(std::static_pointer_cast<rclcpp::ServiceBase>(server),
                                                  std::move(callback_group));
  return server;
}

TEST(GenericServiceTest, ServesRequestsThroughCallback) {
  auto server_node = std::make_shared<rclcpp::Node>("gs_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("gs_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  auto server = makeServer(*server_node, callback_group, "/gs/set_bool", setBoolEchoCallback);
  ASSERT_NE(server, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  auto client = client_node->create_client<std_srvs::srv::SetBool>("/gs/set_bool");
  ASSERT_TRUE(client->wait_for_service(3s));

  auto request = std::make_shared<std_srvs::srv::SetBool::Request>();
  request->data = true;
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(3s), std::future_status::ready);
  const auto response = future.get();
  EXPECT_TRUE(response->success);
  EXPECT_EQ(response->message, "served-on");
}

TEST(GenericServiceTest, DropsResponseWhenCallbackReturnsNullopt) {
  auto server_node = std::make_shared<rclcpp::Node>("gs_drop_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("gs_drop_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  auto server =
      makeServer(*server_node, callback_group, "/gs/drop",
                 [](std::vector<std::uint8_t>) -> std::optional<std::vector<std::uint8_t>> { return std::nullopt; });
  ASSERT_NE(server, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  auto client = client_node->create_client<std_srvs::srv::SetBool>("/gs/drop");
  ASSERT_TRUE(client->wait_for_service(3s));

  auto request = std::make_shared<std_srvs::srv::SetBool::Request>();
  request->data = true;
  auto future = client->async_send_request(request);
  EXPECT_EQ(future.wait_for(500ms), std::future_status::timeout);
}

} // namespace
} // namespace ros2_livekit_bridge::ros2_cli
