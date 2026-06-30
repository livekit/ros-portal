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

#include <rcl/error_handling.h>
#include <rcl/service.h>

#include <cstring>
#include <exception>
#include <memory>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <string>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"

namespace ros2_livekit_bridge::ros2_cli {

GenericService::GenericService(std::shared_ptr<rcl_node_t> node_handle, const std::string &service_name,
                               std::shared_ptr<ServiceTypeSupport> support, RequestCallback callback)
    : rclcpp::ServiceBase(node_handle), support_(std::move(support)), callback_(std::move(callback)) {
  // rcl does the static memory allocation here. Mirrors rclcpp::Service<T>: the
  // ServiceBase ctor does not allocate the handle, so we allocate it ourselves
  // with an rcl_service_fini deleter that keeps the node handle alive.
  service_handle_ =
      std::shared_ptr<rcl_service_t>(new rcl_service_t, [handle = node_handle_, service_name](rcl_service_t *service) {
        if (rcl_service_fini(service, handle.get()) != RCL_RET_OK) {
          RCLCPP_ERROR(rclcpp::get_node_logger(handle.get()).get_child("rclcpp"),
                       "Error in destruction of rcl service handle: %s", rcl_get_error_string().str);
          rcl_reset_error();
        }
        delete service;
      });
  *service_handle_.get() = rcl_get_zero_initialized_service();

  // TODO: derive service QoS from the real endpoint / config instead of
  // defaults. rcl_service_get_default_options() uses the generic
  // rmw_qos_profile_services_default profile.
  rcl_service_options_t options = rcl_service_get_default_options();
  const rcl_ret_t ret =
      rcl_service_init(service_handle_.get(), node_handle.get(), support_->handle, service_name.c_str(), &options);
  if (ret != RCL_RET_OK) {
    if (ret == RCL_RET_SERVICE_NAME_INVALID) {
      auto *rcl_node_handle = get_rcl_node_handle();
      rcl_reset_error();
      rclcpp::expand_topic_or_service_name(service_name, rcl_node_get_name(rcl_node_handle),
                                           rcl_node_get_namespace(rcl_node_handle), true);
    }
    rclcpp::exceptions::throw_from_rcl_error(ret, "could not create generic service");
  }
}

std::shared_ptr<void> GenericService::create_request() {
  // Allocate runtime request storage and return an aliasing shared_ptr to its
  // raw buffer, keeping the type support alive alongside the message. Mirrors
  // Ros2ServiceCall::ServiceClient::create_response.
  struct RequestStorage {
    explicit RequestStorage(std::shared_ptr<ServiceTypeSupport> type_support)
        : support(std::move(type_support)),
          message(support->request.members, rosidl_runtime_cpp::MessageInitialization::ZERO) {}

    std::shared_ptr<ServiceTypeSupport> support;
    DynamicMessage message;
  };

  auto storage = std::make_shared<RequestStorage>(support_);
  return std::shared_ptr<void>(storage, storage->message.data());
}

std::shared_ptr<rmw_request_id_t> GenericService::create_request_header() {
  return std::make_shared<rmw_request_id_t>();
}

void GenericService::handle_request(std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> request) {
  // 1) Serialize the typed request message into CDR for the LiveKit RPC.
  rclcpp::SerializedMessage serialized_request;
  try {
    support_->request.serializer.serialize_message(request.get(), &serialized_request);
  } catch (const std::exception &error) {
    RCLCPP_ERROR(node_logger_.get_child("rclcpp"), "Failed to serialize request for service '%s': %s",
                 get_service_name(), error.what());
    return;
  }

  const auto &rcl_request = serialized_request.get_rcl_serialized_message();
  std::vector<std::uint8_t> request_cdr(rcl_request.buffer, rcl_request.buffer + rcl_request.buffer_length);

  // 2) Forward to the callback (which performs the blocking LiveKit RPC). A
  // std::nullopt result drops the response and lets the ROS caller time out.
  const auto response_cdr = callback_(std::move(request_cdr));
  if (!response_cdr) {
    return;
  }

  // 3) Deserialize the CDR response back into a typed response message.
  DynamicMessage response_message(support_->response.members, rosidl_runtime_cpp::MessageInitialization::ZERO);
  rclcpp::SerializedMessage serialized_response(response_cdr->size());
  auto &rcl_response = serialized_response.get_rcl_serialized_message();
  if (!response_cdr->empty()) {
    std::memcpy(rcl_response.buffer, response_cdr->data(), response_cdr->size());
  }
  rcl_response.buffer_length = response_cdr->size();
  try {
    support_->response.serializer.deserialize_message(&serialized_response, response_message.data());
  } catch (const std::exception &error) {
    RCLCPP_ERROR(node_logger_.get_child("rclcpp"), "Failed to deserialize response for service '%s': %s",
                 get_service_name(), error.what());
    return;
  }

  // 4) Send the response back to the ROS caller.
  const rcl_ret_t ret = rcl_send_response(get_service_handle().get(), request_header.get(), response_message.data());
  if (ret == RCL_RET_TIMEOUT) {
    RCLCPP_WARN(node_logger_.get_child("rclcpp"), "Failed to send response to '%s' (timeout): %s", get_service_name(),
                rcl_get_error_string().str);
    rcl_reset_error();
    return;
  }
  if (ret != RCL_RET_OK) {
    RCLCPP_ERROR(node_logger_.get_child("rclcpp"), "Failed to send response to '%s': %s", get_service_name(),
                 rcl_get_error_string().str);
    rcl_reset_error();
  }
}

} // namespace ros2_livekit_bridge::ros2_cli
