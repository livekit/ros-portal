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

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rosidl_runtime_c/service_type_support_struct.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/client.hpp>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <stdexcept>
#include <thread>
#include <utility>

#include "ros2_livekit_bridge/introspection/dynamic_message.hpp"
#include "ros2_livekit_bridge/introspection/introspection_utils.hpp"
#include "ros2_livekit_bridge/introspection/runtime_type_support.hpp"
#include "ros2_livekit_bridge/ros2_cli/constants.hpp"

namespace ros2_livekit_bridge::ros2_cli {

using Clock = std::chrono::steady_clock;

constexpr auto kPollPeriod = std::chrono::milliseconds(2);
// Per-call ceiling on stale (mismatched) responses drained from the reader
// before giving up this poll iteration. The client uses KEEP_LAST depth 10, so
// the reader holds at most ~10 samples; this comfortably exceeds that and only
// trips if the queue is being refilled faster than it drains.
constexpr std::uint8_t kMaxStaleResponseDrains = 25;

/// @brief Runtime service client for an arbitrary service type.
struct Ros2ServiceCall::ServiceClient : public rclcpp::ClientBase {
  /// @brief Construct a ClientBase-backed runtime service client.
  ServiceClient(const std::string &service_name, const std::string &msg_type,
                std::shared_ptr<introspection::RuntimeServiceTypeSupport> support,
                rclcpp::node_interfaces::NodeBaseInterface *node_base,
                rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph)
      : rclcpp::ClientBase(node_base, std::move(node_graph)), msg_type(msg_type), support(std::move(support)) {
    rcl_client_options_t options = rcl_client_get_default_options();
    const rcl_ret_t ret = rcl_client_init(get_client_handle().get(), get_rcl_node_handle(), this->support->handle,
                                          service_name.c_str(), &options);
    if (ret != RCL_RET_OK) {
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        rcl_reset_error();
        rclcpp::expand_topic_or_service_name(service_name, rcl_node_get_name(get_rcl_node_handle()),
                                             rcl_node_get_namespace(get_rcl_node_handle()), true);
      }
      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create service client");
    }
  }

  /// @brief Create response storage for executor APIs.
  std::shared_ptr<void> create_response() override {
    struct ResponseStorage {
      /// @brief Keep type support alive beside response memory.
      explicit ResponseStorage(std::shared_ptr<introspection::RuntimeServiceTypeSupport> type_support)
          : support(std::move(type_support)),
            message(support->response.members, rosidl_runtime_cpp::MessageInitialization::ZERO) {}

      /// @brief Type support used to initialize message storage.
      std::shared_ptr<introspection::RuntimeServiceTypeSupport> support;
      /// @brief Runtime response message storage.
      introspection::DynamicMessage message;
    };

    auto storage = std::make_shared<ResponseStorage>(support);
    return std::shared_ptr<void>(storage, storage->message.data());
  }

  /// @brief Create response header storage for executor APIs.
  std::shared_ptr<rmw_request_id_t> create_request_header() override { return std::make_shared<rmw_request_id_t>(); }

  /// @brief Executor response hook; direct polling consumes responses here.
  /// @note This overrides the virtual implementation in ClientBase.
  void handle_response(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> response) override {
    (void)request_header;
    (void)response;
  }

  /// @brief Service type used by this client.
  std::string msg_type;
  /// @brief Service, request, and response type support.
  std::shared_ptr<introspection::RuntimeServiceTypeSupport> support;
  /// @brief Serializes send+take on this client across concurrent callers.
  std::mutex call_mutex;
};

Ros2ServiceCall::Ros2ServiceCall(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
                                 rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph, rclcpp::Logger logger)
    : base_(std::move(base)), graph_(std::move(graph)), logger_(std::move(logger)) {
  if (!base_ || !graph_) {
    throw std::invalid_argument("Ros2ServiceCall requires node base and graph interfaces");
  }
}

Ros2ServiceCall::~Ros2ServiceCall() = default;

Ros2ServiceCallSrv::Response Ros2ServiceCall::call(ServiceCallOptions options) {
  std::string resolved_service;
  try {
    resolved_service =
        rclcpp::expand_topic_or_service_name(options.service, base_->get_name(), base_->get_namespace(), true);
  } catch (const std::exception &error) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, error.what());
  }

  if (options.msg_type.empty()) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, "msg_type must be non-empty");
  }

  const auto &msg_type = options.msg_type;

  ClientPtr client;
  std::string client_error;
  if (auto resolved_client = getClient(resolved_service, msg_type, client_error)) {
    client = std::move(*resolved_client);
  } else {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(
        false, std::string("failed to create service client: ") + client_error);
  }

  introspection::DynamicMessage request_message(client->support->request.members,
                                                rosidl_runtime_cpp::MessageInitialization::ZERO);
  std::string yaml_error;
  if (!introspection::populateMessageFromYaml(options.msg_type + "_Request", options.payload, request_message.data(),
                                              yaml_error)) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, "failed to build service request: " + yaml_error);
  }

  // Serialize all send+take operations on the same client so concurrent
  // callers cannot interleave rcl_send_request / take_type_erased_response
  // and steal each other's responses.
  std::lock_guard<std::mutex> client_lock(client->call_mutex);

  // rcl_send_request assigns the outgoing sequence number via this out-param.
  std::int64_t sequence_number = 0;
  const rcl_ret_t send_ret =
      rcl_send_request(client->get_client_handle().get(), request_message.data(), &sequence_number);
  if (send_ret != RCL_RET_OK) {
    const std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, "failed to send service request: " + message);
  }

  const auto timeout = std::chrono::seconds(options.timeout_sec == 0 ? kDefaultTimeoutSec : options.timeout_sec);
  const auto deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    auto response = takeResponse(*client, sequence_number);
    if (response) {
      return *response;
    }
    std::this_thread::sleep_for(kPollPeriod);
  }

  return makeCliResponse<Ros2ServiceCallSrv::Response>(false, "Service call timed out.");
}

std::optional<Ros2ServiceCall::ClientPtr> Ros2ServiceCall::getClient(const std::string &service,
                                                                     const std::string &msg_type, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = service + ":" + msg_type;
  const auto existing = clients_.find(key);
  if (existing != clients_.end()) {
    return existing->second;
  }
  if (clients_.size() >= kMaxCachedServiceClients) {
    error = "service client cache limit reached";
    return std::nullopt;
  }

  auto support = introspection::RuntimeServiceTypeSupport::create(msg_type, error);
  if (!support) {
    return std::nullopt;
  }

  try {
    auto client = std::make_shared<ServiceClient>(service, msg_type, std::move(support), base_.get(), graph_);
    return clients_.emplace(key, std::move(client)).first->second;
  } catch (const std::exception &exception) {
    error = exception.what();
    return std::nullopt;
  }
}

std::optional<Ros2ServiceCallSrv::Response> Ros2ServiceCall::takeResponse(ServiceClient &client,
                                                                          std::int64_t sequence_number) {
  std::uint8_t attempt_count = 0;
  // Bounded two ways: each iteration either:
  // 1. take_type_erased_response has no more items in the rmq queue to take
  // 2. takes a response with a mismatched sequence number > kMaxStaleResponseDrains
  while (attempt_count < kMaxStaleResponseDrains) {
    introspection::DynamicMessage response_message(client.support->response.members,
                                                   rosidl_runtime_cpp::MessageInitialization::ZERO);
    rmw_request_id_t header{};
    if (!client.take_type_erased_response(response_message.data(), header)) {
      return std::nullopt;
    }
    if (header.sequence_number != sequence_number) {
      ++attempt_count;
      continue;
    }

    try {
      const auto output = introspection::toYaml(client.msg_type + "_Response", response_message.data());
      if (!output) {
        return makeCliResponse<Ros2ServiceCallSrv::Response>(
            false, "failed to convert service response: unknown message type '" + client.msg_type + "_Response'");
      }
      return makeCliResponse<Ros2ServiceCallSrv::Response>(true, "", *output);
    } catch (const std::exception &error) {
      return makeCliResponse<Ros2ServiceCallSrv::Response>(
          false, std::string("failed to convert service response: ") + error.what());
    }
  }

  RCLCPP_WARN(logger_, "Failed to take response after %u attempts.", static_cast<unsigned>(kMaxStaleResponseDrains));
  return std::nullopt;
}

} // namespace ros2_livekit_bridge::ros2_cli
