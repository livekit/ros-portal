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

#include "ros2_livekit_bridge/service_forwarder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <string>
#include <thread>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"

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

private:
  rclcpp::executors::SingleThreadedExecutor &executor_;
  std::thread spin_thread_;
};

struct FakeLiveKit {
  bool has_participant{true};
  int rpc_calls{0};
  std::string last_participant;
  std::string last_method;
  std::string last_payload;
  std::optional<std::string> rpc_response{
      cliResponseToJson(true, "", "success: true\nmessage: enabled\n"),
  };

  ServiceForwarder::LiveKitMethods methods() {
    ServiceForwarder::LiveKitMethods methods;
    methods.has_participant = [this](const std::string &) { return has_participant; };
    methods.perform_rpc = [this](const std::string &participant, const std::string &method, const std::string &payload,
                                 std::uint8_t) -> std::optional<std::string> {
      ++rpc_calls;
      last_participant = participant;
      last_method = method;
      last_payload = payload;
      return rpc_response;
    };
    return methods;
  }
};

ServiceForwarder::ServiceRoute setBoolRoute(const std::string &service_name = "/service_forwarder/set_bool") {
  return ServiceForwarder::ServiceRoute{
      service_name,
      "std_srvs/srv/SetBool",
      "robot-b",
  };
}

std_srvs::srv::SetBool::Response::SharedPtr callSetBool(rclcpp::Node &node, const std::string &service_name,
                                                        bool data) {
  auto client = node.create_client<std_srvs::srv::SetBool>(service_name);
  if (!client->wait_for_service(2s)) {
    return nullptr;
  }

  auto request = std::make_shared<std_srvs::srv::SetBool::Request>();
  request->data = data;
  auto future = client->async_send_request(request);
  if (future.wait_for(2s) != std::future_status::ready) {
    return nullptr;
  }
  return future.get();
}

class ServiceForwarderTest : public ::testing::Test {};

TEST_F(ServiceForwarderTest, ForwardsSetBoolRequestAndPopulatesResponse) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;

  ServiceForwarder forwarder({setBoolRoute()}, *server_node, callback_group, livekit.methods());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*client_node, "/service_forwarder/set_bool"));
  const auto response = callSetBool(*client_node, "/service_forwarder/set_bool", true);

  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success);
  EXPECT_EQ(response->message, "enabled");
  EXPECT_EQ(livekit.rpc_calls, 1);
  EXPECT_EQ(livekit.last_participant, "robot-b");
  EXPECT_EQ(livekit.last_method, ros2_cli::kServiceCallRpcMethod);

  const auto payload = nlohmann::json::parse(livekit.last_payload);
  EXPECT_EQ(payload.at("service"), "/service_forwarder/set_bool");
  EXPECT_EQ(payload.at("msg_type"), "std_srvs/srv/SetBool");
  EXPECT_NE(payload.at("payload").get<std::string>().find("data: true"), std::string::npos);
}

TEST_F(ServiceForwarderTest, MissingParticipantReturnsDefaultResponse) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_missing_participant_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_missing_participant_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.has_participant = false;

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/missing_participant")}, *server_node, callback_group,
                             livekit.methods());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*client_node, "/service_forwarder/missing_participant"));
  const auto response = callSetBool(*client_node, "/service_forwarder/missing_participant", true);

  ASSERT_NE(response, nullptr);
  EXPECT_FALSE(response->success);
  EXPECT_EQ(response->message, "");
  EXPECT_EQ(livekit.rpc_calls, 0);
}

TEST_F(ServiceForwarderTest, MalformedRpcResponseReturnsDefaultResponse) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_malformed_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_malformed_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.rpc_response = "not-json";

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/malformed")}, *server_node, callback_group,
                             livekit.methods());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*client_node, "/service_forwarder/malformed"));
  const auto response = callSetBool(*client_node, "/service_forwarder/malformed", true);

  ASSERT_NE(response, nullptr);
  EXPECT_FALSE(response->success);
  EXPECT_EQ(response->message, "");
  EXPECT_EQ(livekit.rpc_calls, 1);
}

} // namespace
} // namespace ros2_livekit_bridge
