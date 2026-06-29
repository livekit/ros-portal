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

#include "ros2_livekit_bridge/introspection/message_render.hpp"
#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"
#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rclcpp/client.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/serialization.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_runtime_c/service_type_support_struct.h>
#include <rosidl_runtime_cpp/message_initialization.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

using Clock = std::chrono::steady_clock;

constexpr auto kPollPeriod = std::chrono::milliseconds(2);
constexpr char kServiceTypeSupportSymbolPrefix[] =
  "__get_service_type_support_handle__";

std::string serviceTypeSupportSymbol(
  const std::string & type,
  const std::string & typesupport_identifier)
{
  std::string symbol = typesupport_identifier + kServiceTypeSupportSymbolPrefix;
  for (const char ch : type) {
    if (ch == '/') {
      symbol += "__";
    } else {
      symbol += ch;
    }
  }
  return symbol;
}

const rosidl_service_type_support_t * Ros2ServiceCall::serviceTypeSupportHandle(
  const std::string & type,
  const std::string & typesupport_identifier,
  rcpputils::SharedLibrary & library)
{
  const std::string symbol =
    serviceTypeSupportSymbol(type, typesupport_identifier);
  if (!library.has_symbol(symbol)) {
    return nullptr;
  }
  using GetServiceTypeSupportHandleFn =
    const rosidl_service_type_support_t * (*)();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto get_handle = reinterpret_cast<GetServiceTypeSupportHandleFn>(
    library.get_symbol(symbol));
  return get_handle();
}

Ros2ServiceCall::ServiceTypeSupport::ServiceTypeSupport(
  const std::string & type,
  std::shared_ptr<rcpputils::SharedLibrary> library,
  const rosidl_service_type_support_t * handle)
: library(std::move(library)),
  handle(handle),
  request(type + kRequestMessageTypeSuffix),
  response(type + kResponseMessageTypeSuffix)
{
}

std::shared_ptr<Ros2ServiceCall::ServiceTypeSupport>
Ros2ServiceCall::ServiceTypeSupport::create(
  const std::string & type,
  std::string & error)
{
  try {
    auto library = rclcpp::get_typesupport_library(
      type, rosidl_typesupport_cpp::typesupport_identifier);
    const auto * handle = serviceTypeSupportHandle(
      type, rosidl_typesupport_cpp::typesupport_identifier, *library);
    if (handle == nullptr) {
      error = "Service typesupport symbol not found: " +
        serviceTypeSupportSymbol(
          type, rosidl_typesupport_cpp::typesupport_identifier);
      return nullptr;
    }
    return std::shared_ptr<ServiceTypeSupport>(
      new ServiceTypeSupport(type, std::move(library), handle));
  } catch (const std::exception & exception) {
    error = exception.what();
    return nullptr;
  }
}

#ifdef BUILD_TESTING
std::string Ros2ServiceCall::serviceTypeSupportCreationError(
  const std::string & type)
{
  std::string error;
  (void)ServiceTypeSupport::create(type, error);
  return error;
}
#endif

Ros2ServiceCall::Ros2ServiceCall(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph)
: base_(std::move(base)),
  graph_(std::move(graph))
{
  if (!base_ || !graph_) {
    throw std::invalid_argument(
      "Ros2ServiceCall requires node base and graph interfaces");
  }
}

Ros2ServiceCall::~Ros2ServiceCall() = default;

Ros2ServiceCallSrv::Response Ros2ServiceCall::call(ServiceCallOptions options)
{
  std::string resolved_service;
  try {
    resolved_service = rclcpp::expand_topic_or_service_name(
      options.service, base_->get_name(), base_->get_namespace(), true);
  } catch (const std::exception & error) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, error.what());
  }

  if (options.msg_type.empty()) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false, "msg_type must be non-empty");
  }

  // The request arrives as native YAML; serialize it into the request type on
  // this side, mirroring how `ros2 topic pub` is handled.
  std::string yaml_error;
  auto serialized_request = serializedMessageFromYaml(
    options.msg_type + "_Request", options.payload, yaml_error);
  if (!serialized_request) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(
      false, "failed to build service request: " + yaml_error);
  }

  const auto & msg_type = options.msg_type;

  ClientPtr client;
  std::string client_error;
  if (auto resolved_client = getClient(resolved_service, msg_type, client_error)) {
    client = std::move(*resolved_client);
  } else {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(
      false, std::string("failed to create service client: ") +
               client_error);
  }

  DynamicMessage request_message(
    client->support->request.members,
    rosidl_runtime_cpp::MessageInitialization::ZERO);
  try {
    client->support->request.serializer.deserialize_message(
      &*serialized_request, request_message.data());
  } catch (const std::exception & error) {
    return makeCliResponse<Ros2ServiceCallSrv::Response>(
      false, std::string("failed to build service request: ") +
               error.what());
  }

  // Serialize all send+take operations on the same client so concurrent
  // callers cannot interleave rcl_send_request / take_type_erased_response
  // and steal each other's responses.
  std::lock_guard<std::mutex> client_lock(client->call_mutex);

  std::int64_t sequence_number = 0;
  const rcl_ret_t send_ret = rcl_send_request(
    client->get_client_handle().get(), request_message.data(),
    &sequence_number);
  if (send_ret != RCL_RET_OK) {
    const std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    return makeCliResponse<Ros2ServiceCallSrv::Response>(false,
        "failed to send service request: " + message);
  }

  const auto timeout = std::chrono::seconds(
    options.timeout_sec == 0 ? kDefaultTimeoutSec : options.timeout_sec);
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

std::optional<Ros2ServiceCall::ClientPtr> Ros2ServiceCall::getClient(
  const std::string & service,
  const std::string & msg_type,
  std::string & error)
{
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

  auto support = ServiceTypeSupport::create(msg_type, error);
  if (!support) {
    return std::nullopt;
  }

  try {
    auto client = std::make_shared<ServiceClient>(
      service, msg_type, std::move(support), base_.get(), graph_);
    return clients_.emplace(key, std::move(client)).first->second;
  } catch (const std::exception & exception) {
    error = exception.what();
    return std::nullopt;
  }
}

std::optional<Ros2ServiceCallSrv::Response> Ros2ServiceCall::takeResponse(
  ServiceClient & client,
  std::int64_t sequence_number)
{
  while (true) {
    DynamicMessage response_message(
      client.support->response.members,
      rosidl_runtime_cpp::MessageInitialization::ZERO);
    rmw_request_id_t header{};
    if (!client.take_type_erased_response(response_message.data(), header)) {
      return std::nullopt;
    }
    if (header.sequence_number != sequence_number) {
      continue;
    }

    try {
      const auto output = message_render::toYaml(
        client.support->response.members, response_message.data());
      return makeCliResponse<Ros2ServiceCallSrv::Response>(true, "", output);
    } catch (const std::exception & error) {
      return makeCliResponse<Ros2ServiceCallSrv::Response>(
        false, std::string("failed to convert service response: ") +
                 error.what());
    }
  }
}

}  // namespace ros2_livekit_bridge::ros2_cli
