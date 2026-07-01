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

#include "ros2_livekit_bridge/service_forwarding/generic_service_client.hpp"

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rcl/node.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <rclcpp/client.hpp>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"

namespace ros2_livekit_bridge {

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kPollPeriod = std::chrono::milliseconds(2);
// Per-call ceiling on stale (mismatched) responses drained from the reader
// before giving up this poll iteration.
constexpr std::uint8_t kMaxStaleResponseDrains = 25;
} // namespace

/// @brief Runtime service client for an arbitrary service type.
struct GenericServiceClient::Client : public rclcpp::ClientBase {
  Client(const std::string &service_name, std::shared_ptr<ServiceTypeSupport> support,
         rclcpp::node_interfaces::NodeBaseInterface *node_base,
         rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph)
      : rclcpp::ClientBase(node_base, std::move(node_graph)), support(std::move(support)) {
    // TODO: derive service QoS from the real endpoint / config instead of
    // defaults. rcl_client_get_default_options() uses the generic services QoS.
    rcl_client_options_t options = rcl_client_get_default_options();
    const rcl_ret_t ret = rcl_client_init(get_client_handle().get(), get_rcl_node_handle(), this->support->handle,
                                          service_name.c_str(), &options);
    if (ret != RCL_RET_OK) {
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        rcl_reset_error();
        rclcpp::expand_topic_or_service_name(service_name, rcl_node_get_name(get_rcl_node_handle()),
                                             rcl_node_get_namespace(get_rcl_node_handle()), true);
      }
      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create generic service client");
    }
  }

  std::shared_ptr<void> create_response() override {
    struct ResponseStorage {
      explicit ResponseStorage(std::shared_ptr<ServiceTypeSupport> type_support)
          : support(std::move(type_support)),
            message(support->response.members, rosidl_runtime_cpp::MessageInitialization::ZERO) {}

      std::shared_ptr<ServiceTypeSupport> support;
      ros2_cli::DynamicMessage message;
    };

    auto storage = std::make_shared<ResponseStorage>(support);
    return std::shared_ptr<void>(storage, storage->message.data());
  }

  std::shared_ptr<rmw_request_id_t> create_request_header() override { return std::make_shared<rmw_request_id_t>(); }

  void handle_response(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> response) override {
    (void)request_header;
    (void)response;
  }

  std::shared_ptr<ServiceTypeSupport> support;
  /// @brief Serializes send+take across concurrent callers.
  std::mutex call_mutex;
};

GenericServiceClient::GenericServiceClient(const std::string &service_name, std::shared_ptr<ServiceTypeSupport> support,
                                           rclcpp::node_interfaces::NodeBaseInterface *node_base,
                                           rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph,
                                           rclcpp::Logger logger)
    : support_(std::move(support)), logger_(std::move(logger)) {
  client_ = std::make_shared<Client>(service_name, support_, node_base, std::move(node_graph));
}

GenericServiceClient::~GenericServiceClient() = default;

bool GenericServiceClient::serviceIsReady() const { return client_->service_is_ready(); }

std::optional<std::vector<std::uint8_t>> GenericServiceClient::call(const std::vector<std::uint8_t> &request_cdr,
                                                                    std::chrono::nanoseconds timeout) {
  // Deserialize the CDR request into a runtime request message.
  ros2_cli::DynamicMessage request_message(support_->request.members, rosidl_runtime_cpp::MessageInitialization::ZERO);
  rclcpp::SerializedMessage serialized_request(request_cdr.size());
  auto &rcl_request = serialized_request.get_rcl_serialized_message();
  if (!request_cdr.empty()) {
    std::memcpy(rcl_request.buffer, request_cdr.data(), request_cdr.size());
  }
  rcl_request.buffer_length = request_cdr.size();
  try {
    support_->request.serializer.deserialize_message(&serialized_request, request_message.data());
  } catch (const std::exception &error) {
    RCLCPP_WARN(logger_, "Failed to deserialize inbound service request: %s", error.what());
    return std::nullopt;
  }

  // Serialize send+take on the client so concurrent callers cannot steal each
  // other's responses.
  std::lock_guard<std::mutex> client_lock(client_->call_mutex);

  std::int64_t sequence_number = 0;
  const rcl_ret_t send_ret =
      rcl_send_request(client_->get_client_handle().get(), request_message.data(), &sequence_number);
  if (send_ret != RCL_RET_OK) {
    const std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    RCLCPP_WARN(logger_, "Failed to send service request: %s", message.c_str());
    return std::nullopt;
  }

  const auto deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    auto response = takeResponse(sequence_number);
    if (response) {
      return response;
    }
    std::this_thread::sleep_for(kPollPeriod);
  }

  RCLCPP_WARN(logger_, "Service call timed out.");
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> GenericServiceClient::takeResponse(std::int64_t sequence_number) {
  std::uint8_t attempt_count = 0;
  while (attempt_count < kMaxStaleResponseDrains) {
    ros2_cli::DynamicMessage response_message(support_->response.members,
                                              rosidl_runtime_cpp::MessageInitialization::ZERO);
    rmw_request_id_t header{};
    if (!client_->take_type_erased_response(response_message.data(), header)) {
      return std::nullopt;
    }
    if (header.sequence_number != sequence_number) {
      ++attempt_count;
      continue;
    }

    rclcpp::SerializedMessage serialized_response;
    try {
      support_->response.serializer.serialize_message(response_message.data(), &serialized_response);
    } catch (const std::exception &error) {
      RCLCPP_WARN(logger_, "Failed to serialize service response: %s", error.what());
      return std::nullopt;
    }
    const auto &rcl_response = serialized_response.get_rcl_serialized_message();
    return std::vector<std::uint8_t>(rcl_response.buffer, rcl_response.buffer + rcl_response.buffer_length);
  }

  RCLCPP_WARN(logger_, "Failed to take response after %u attempts.", static_cast<unsigned>(kMaxStaleResponseDrains));
  return std::nullopt;
}

} // namespace ros2_livekit_bridge
