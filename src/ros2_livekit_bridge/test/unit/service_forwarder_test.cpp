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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ros2_livekit_bridge/service_rpc_codec.hpp"
#include "ros2_livekit_bridge/types.hpp"

namespace ros2_livekit_bridge {
namespace {

using namespace std::chrono_literals;
using SetBool = std_srvs::srv::SetBool;

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
  SetBool::Request request;
  request.data = data;
  rclcpp::Serialization<SetBool::Request> serializer;
  rclcpp::SerializedMessage message;
  serializer.serialize_message(&request, &message);
  const auto &rcl = message.get_rcl_serialized_message();
  return std::vector<std::uint8_t>(rcl.buffer, rcl.buffer + rcl.buffer_length);
}

SetBool::Request deserializeSetBoolRequest(const std::vector<std::uint8_t> &cdr) {
  rclcpp::SerializedMessage message(cdr.size());
  auto &rcl = message.get_rcl_serialized_message();
  std::memcpy(rcl.buffer, cdr.data(), cdr.size());
  rcl.buffer_length = cdr.size();
  rclcpp::Serialization<SetBool::Request> serializer;
  SetBool::Request request;
  serializer.deserialize_message(&message, &request);
  return request;
}

std::vector<std::uint8_t> serializeSetBoolResponse(const SetBool::Response &response) {
  rclcpp::Serialization<SetBool::Response> serializer;
  rclcpp::SerializedMessage message;
  serializer.serialize_message(&response, &message);
  const auto &rcl = message.get_rcl_serialized_message();
  return std::vector<std::uint8_t>(rcl.buffer, rcl.buffer + rcl.buffer_length);
}

SetBool::Response deserializeSetBoolResponse(const std::vector<std::uint8_t> &cdr) {
  rclcpp::SerializedMessage message(cdr.size());
  auto &rcl = message.get_rcl_serialized_message();
  std::memcpy(rcl.buffer, cdr.data(), cdr.size());
  rcl.buffer_length = cdr.size();
  rclcpp::Serialization<SetBool::Response> serializer;
  SetBool::Response response;
  serializer.deserialize_message(&message, &response);
  return response;
}

// Records calls the forwarder makes and produces a ServiceForwarder::
// LiveKitMethods whose callbacks drive this recorder.
class FakeRpc {
public:
  std::atomic_bool has_participant{true};
  std::function<std::optional<std::string>(const std::string &payload)> on_perform_rpc;

  std::mutex mutex;
  std::string last_participant;
  std::string last_method;
  std::string last_payload;
  std::unordered_map<std::string, RpcHandler> handlers;
  std::vector<std::string> registered_methods;
  std::vector<std::string> unregistered_methods;

  ServiceForwarder::LiveKitMethods make() {
    ServiceForwarder::LiveKitMethods methods;
    methods.has_participant = [this](const std::string &) { return has_participant.load(); };
    methods.perform_rpc = [this](const std::string &participant, const std::string &method, const std::string &payload,
                                 std::uint8_t) -> std::optional<std::string> {
      {
        std::lock_guard<std::mutex> lock(mutex);
        last_participant = participant;
        last_method = method;
        last_payload = payload;
      }
      if (on_perform_rpc) {
        return on_perform_rpc(payload);
      }
      return std::nullopt;
    };
    methods.register_rpc_method = [this](const std::string &method, RpcHandler handler) {
      std::lock_guard<std::mutex> lock(mutex);
      registered_methods.push_back(method);
      handlers[method] = std::move(handler);
      return true;
    };
    methods.unregister_rpc_method = [this](const std::string &method) {
      std::lock_guard<std::mutex> lock(mutex);
      unregistered_methods.push_back(method);
      return true;
    };
    return methods;
  }

  RpcHandler handlerFor(const std::string &method) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = handlers.find(method);
    return it == handlers.end() ? RpcHandler{} : it->second;
  }
};

ServiceForwarder::ServiceForwarderEntry makeEntry(const std::string &service, ServiceForwarder::Direction direction) {
  ServiceForwarder::ServiceForwarderEntry entry;
  entry.service = service;
  entry.msg_type = "std_srvs/srv/SetBool";
  entry.direction = direction;
  entry.participant = "peer";
  return entry;
}

TEST(ServiceForwarderTest, RejectsExpiredNode) {
  rclcpp::Node::WeakPtr weak_node;
  {
    auto node = std::make_shared<rclcpp::Node>("sf_expired_node");
    weak_node = node;
  }
  FakeRpc fake;
  ServiceForwarder::ServiceForwarderOptions options;
  EXPECT_THROW({ ServiceForwarder forwarder(options, weak_node, fake.make()); }, std::invalid_argument);
}

TEST(ServiceForwarderTest, RejectsUnsetLiveKitMethods) {
  auto node = std::make_shared<rclcpp::Node>("sf_unset_node");
  ServiceForwarder::ServiceForwarderOptions options;
  ServiceForwarder::LiveKitMethods methods; // all callbacks unset
  EXPECT_THROW({ ServiceForwarder forwarder(options, node, std::move(methods)); }, std::invalid_argument);
}

TEST(ServiceForwarderTest, RpcMethodNameIsDeterministicAndSanitized) {
  EXPECT_EQ(ServiceForwarder::rpcMethodName("/turtle1/set_pen"), "ros2_srv:turtle1_set_pen");
  EXPECT_EQ(ServiceForwarder::rpcMethodName("/set_bool"), "ros2_srv:set_bool");
  EXPECT_EQ(ServiceForwarder::rpcMethodName("/turtle1/set_pen"), ServiceForwarder::rpcMethodName("/turtle1/set_pen"));
}

TEST(ServiceForwarderTest, RpcMethodNameStaysWithinLimitForLongNames) {
  const std::string long_service = "/" + std::string(200, 'a');
  const auto name = ServiceForwarder::rpcMethodName(long_service);
  EXPECT_LE(name.size(), 64U);
  EXPECT_EQ(name, ServiceForwarder::rpcMethodName(long_service));
}

TEST(ServiceForwarderTest, InboundRpcCallsLocalService) {
  auto node = std::make_shared<rclcpp::Node>("sf_inbound_node");
  auto server_node = std::make_shared<rclcpp::Node>("sf_inbound_server");

  auto service = server_node->create_service<SetBool>(
      "/sf/in_set_bool", [](const SetBool::Request::SharedPtr request, SetBool::Response::SharedPtr response) {
        response->success = request->data;
        response->message = request->data ? "enabled" : "disabled";
      });
  ASSERT_NE(service, nullptr);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(server_node);
  ScopedExecutorSpin spin_guard(executor);
  ASSERT_TRUE(waitForService(*node, "/sf/in_set_bool"));

  FakeRpc fake;
  ServiceForwarder::ServiceForwarderOptions options;
  options.services.push_back(makeEntry("/sf/in_set_bool", ServiceForwarder::Direction::In));
  ServiceForwarder forwarder(options, node, fake.make());

  const std::string method = ServiceForwarder::rpcMethodName("/sf/in_set_bool");
  const RpcHandler handler = fake.handlerFor(method);
  ASSERT_TRUE(static_cast<bool>(handler));

  const std::string response_payload = handler(encodeServiceRequest(serializeSetBoolRequest(true)));
  const auto decoded = decodeServiceResponse(response_payload);
  ASSERT_TRUE(decoded.ok) << decoded.err;
  const auto response = deserializeSetBoolResponse(decoded.response_cdr);
  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.message, "enabled");
}

TEST(ServiceForwarderTest, OutboundProxyForwardsViaRpc) {
  auto node = std::make_shared<rclcpp::Node>("sf_outbound_node");
  auto client_node = std::make_shared<rclcpp::Node>("sf_outbound_client");

  FakeRpc fake;
  fake.on_perform_rpc = [](const std::string &payload) -> std::optional<std::string> {
    const auto request_cdr = decodeServiceRequest(payload);
    if (!request_cdr) {
      return std::nullopt;
    }
    const auto request = deserializeSetBoolRequest(*request_cdr);
    SetBool::Response response;
    response.success = request.data;
    response.message = "from-rpc";
    return encodeServiceResponse(true, serializeSetBoolResponse(response), "");
  };

  ServiceForwarder::ServiceForwarderOptions options;
  options.services.push_back(makeEntry("/sf/out_set_bool", ServiceForwarder::Direction::Out));
  ServiceForwarder forwarder(options, node, fake.make());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  auto client = client_node->create_client<SetBool>("/sf/out_set_bool");
  ASSERT_TRUE(client->wait_for_service(3s));

  auto request = std::make_shared<SetBool::Request>();
  request->data = true;
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
  const auto response = future.get();
  EXPECT_TRUE(response->success);
  EXPECT_EQ(response->message, "from-rpc");

  std::lock_guard<std::mutex> lock(fake.mutex);
  EXPECT_EQ(fake.last_participant, "peer");
  EXPECT_EQ(fake.last_method, ServiceForwarder::rpcMethodName("/sf/out_set_bool"));
}

TEST(ServiceForwarderTest, OutboundFailsWhenParticipantMissing) {
  auto node = std::make_shared<rclcpp::Node>("sf_missing_node");
  auto client_node = std::make_shared<rclcpp::Node>("sf_missing_client");

  FakeRpc fake;
  fake.has_participant = false;

  ServiceForwarder::ServiceForwarderOptions options;
  options.services.push_back(makeEntry("/sf/missing_set_bool", ServiceForwarder::Direction::Out));
  ServiceForwarder forwarder(options, node, fake.make());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(client_node);
  ScopedExecutorSpin spin_guard(executor);

  auto client = client_node->create_client<SetBool>("/sf/missing_set_bool");
  ASSERT_TRUE(client->wait_for_service(3s));

  auto request = std::make_shared<SetBool::Request>();
  request->data = true;
  auto future = client->async_send_request(request);
  EXPECT_EQ(future.wait_for(800ms), std::future_status::timeout);

  std::lock_guard<std::mutex> lock(fake.mutex);
  EXPECT_TRUE(fake.last_method.empty()); // RPC never attempted
}

TEST(ServiceForwarderTest, DestructorUnregistersInboundMethods) {
  auto node = std::make_shared<rclcpp::Node>("sf_dtor_node");
  FakeRpc fake;

  ServiceForwarder::ServiceForwarderOptions options;
  options.services.push_back(makeEntry("/sf/dtor_set_bool", ServiceForwarder::Direction::In));
  const std::string method = ServiceForwarder::rpcMethodName("/sf/dtor_set_bool");

  {
    ServiceForwarder forwarder(options, node, fake.make());
    std::lock_guard<std::mutex> lock(fake.mutex);
    EXPECT_EQ(fake.registered_methods.size(), 1U);
    EXPECT_EQ(fake.registered_methods[0], method);
  }

  std::lock_guard<std::mutex> lock(fake.mutex);
  ASSERT_EQ(fake.unregistered_methods.size(), 1U);
  EXPECT_EQ(fake.unregistered_methods[0], method);
}

} // namespace
} // namespace ros2_livekit_bridge
