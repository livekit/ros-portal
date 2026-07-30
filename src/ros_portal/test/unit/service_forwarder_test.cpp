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

#include "ros_portal/service_forwarder.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <functional>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <stdexcept>
#include <string>
#include <thread>

#include "ros_portal/cli/constants.hpp"
#include "ros_portal/cli/json_converters.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"

namespace ros_portal {
namespace {

using namespace std::chrono_literals;

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

struct CapturingDiagnostics {
  std::string task_name;
  diagnostics::DiagnosticsManagerFns::TaskCallback callback;

  diagnostics::DiagnosticsManagerFns methods() {
    diagnostics::DiagnosticsManagerFns result;
    result.add = [this](const std::string& name, diagnostics::DiagnosticsManagerFns::TaskCallback task_callback) {
      task_name = name;
      callback = std::move(task_callback);
    };
    result.remove = [this](const std::string& name) {
      if (name == task_name) {
        callback = {};
      }
    };
    return result;
  }

  void populate(diagnostic_updater::DiagnosticStatusWrapper& result) {
    if (callback) {
      callback(result);
    }
  }
};

bool waitForService(rclcpp::Node& node, const std::string& service_name, std::chrono::milliseconds timeout = 2s) {
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

struct FakeLiveKit {
  bool room_available{true};
  bool has_participant{true};
  bool throw_in_has_participant{false};
  int rpc_calls{0};
  std::string last_participant;
  std::string last_method;
  std::string last_payload;
  std::optional<std::string> rpc_response{
      cliResponseToJson(true, "", "success: true\nmessage: enabled\n"),
  };

  ServiceForwarder::LiveKitMethods methods() {
    ServiceForwarder::LiveKitMethods methods;
    methods.is_room_available = [this]() { return room_available; };
    methods.has_participant = [this](const std::string&) {
      if (throw_in_has_participant) {
        throw std::runtime_error("participant lookup failed");
      }
      return has_participant;
    };
    methods.perform_rpc = [this](const std::string& participant, const std::string& method, const std::string& payload,
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

ServiceForwarder::ServiceRoute setBoolRoute(const std::string& service_name = "/service_forwarder/set_bool") {
  return ServiceForwarder::ServiceRoute{
      service_name,
      "std_srvs/srv/SetBool",
      "robot-b",
  };
}

std_srvs::srv::SetBool::Response::SharedPtr callSetBool(rclcpp::Node& node, const std::string& service_name,
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
  CapturingDiagnostics diagnostics;

  ServiceForwarder forwarder({setBoolRoute()}, *server_node, callback_group, livekit.methods(), diagnostics.methods());

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
  EXPECT_EQ(livekit.last_method, cli::kServiceCallRpcMethod);

  const auto payload = nlohmann::json::parse(livekit.last_payload);
  EXPECT_EQ(payload.at("service"), "/service_forwarder/set_bool");
  EXPECT_EQ(payload.at("msg_type"), "std_srvs/srv/SetBool");
  EXPECT_NE(payload.at("payload").get<std::string>().find("data: true"), std::string::npos);

  diagnostic_updater::DiagnosticStatusWrapper status;
  diagnostics.populate(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(valueFor(status, "routes_configured"), "1");
  EXPECT_EQ(valueFor(status, "services_created"), "1");
  EXPECT_EQ(valueFor(status, "requests_forwarded"), "1");
  EXPECT_EQ(valueFor(status, "requests_succeeded"), "1");
  EXPECT_EQ(valueFor(status, "requests_failed"), "0");
}

TEST_F(ServiceForwarderTest, MissingParticipantReturnsDefaultResponse) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_missing_participant_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_missing_participant_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.has_participant = false;
  CapturingDiagnostics diagnostics;

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/missing_participant")}, *server_node, callback_group,
                             livekit.methods(), diagnostics.methods());

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

  diagnostic_updater::DiagnosticStatusWrapper status;
  diagnostics.populate(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(status, "requests_forwarded"), "1");
  EXPECT_EQ(valueFor(status, "requests_failed"), "1");
  EXPECT_EQ(valueFor(status, "failures.participant_not_found"), "1");
  EXPECT_EQ(valueFor(status, "last_failure_service"), "/service_forwarder/missing_participant");
  EXPECT_EQ(valueFor(status, "last_failure_reason"), "participant_not_found");
}

TEST_F(ServiceForwarderTest, RoomUnavailableReturnsExplicitFailureWhenRepresentable) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_room_unavailable_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_room_unavailable_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.room_available = false;

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/room_unavailable")}, *server_node, callback_group,
                             livekit.methods());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*client_node, "/service_forwarder/room_unavailable"));
  const auto response = callSetBool(*client_node, "/service_forwarder/room_unavailable", true);

  ASSERT_NE(response, nullptr);
  EXPECT_FALSE(response->success);
  EXPECT_EQ(response->message, kRoomNotConnectedError);
  EXPECT_EQ(livekit.rpc_calls, 0);
}

TEST_F(ServiceForwarderTest, MalformedRpcResponseReturnsDefaultResponse) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_malformed_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_malformed_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.rpc_response = "not-json";
  CapturingDiagnostics diagnostics;

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/malformed")}, *server_node, callback_group,
                             livekit.methods(), diagnostics.methods());

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

  diagnostic_updater::DiagnosticStatusWrapper status;
  diagnostics.populate(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(status, "failures.malformed_response"), "1");
  EXPECT_EQ(valueFor(status, "last_failure_reason"), "malformed_response");
}

TEST_F(ServiceForwarderTest, DiagnosticsReportSkippedRoutes) {
  auto node = std::make_shared<rclcpp::Node>("service_forwarder_skipped_routes_node");
  auto callback_group = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  CapturingDiagnostics diagnostics;

  ServiceForwarder forwarder(
      {
          setBoolRoute(),
          {"", "std_srvs/srv/SetBool", "robot-b"},
          {"/service_forwarder/missing_type", "missing_msgs/srv/Missing", "robot-b"},
      },
      *node, callback_group, livekit.methods(), diagnostics.methods());

  diagnostic_updater::DiagnosticStatusWrapper status;
  diagnostics.populate(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(valueFor(status, "routes_configured"), "3");
  EXPECT_EQ(valueFor(status, "services_created"), "1");
  EXPECT_EQ(valueFor(status, "routes_skipped_invalid_config"), "1");
  EXPECT_EQ(valueFor(status, "routes_skipped_no_type_support"), "1");
}

TEST_F(ServiceForwarderTest, DiagnosticsReportHandlerExceptions) {
  auto server_node = std::make_shared<rclcpp::Node>("service_forwarder_exception_server_node");
  auto client_node = std::make_shared<rclcpp::Node>("service_forwarder_exception_client_node");
  auto callback_group = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  FakeLiveKit livekit;
  livekit.throw_in_has_participant = true;
  CapturingDiagnostics diagnostics;

  ServiceForwarder forwarder({setBoolRoute("/service_forwarder/exception")}, *server_node, callback_group,
                             livekit.methods(), diagnostics.methods());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  ASSERT_TRUE(waitForService(*client_node, "/service_forwarder/exception"));
  const auto response = callSetBool(*client_node, "/service_forwarder/exception", true);
  ASSERT_NE(response, nullptr);

  diagnostic_updater::DiagnosticStatusWrapper status;
  diagnostics.populate(status);
  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(valueFor(status, "requests_forwarded"), "1");
  EXPECT_EQ(valueFor(status, "requests_failed"), "1");
  EXPECT_EQ(valueFor(status, "handler_exceptions"), "1");
  EXPECT_EQ(valueFor(status, "last_failure_reason"), "handler_exception");
}

} // namespace
} // namespace ros_portal
