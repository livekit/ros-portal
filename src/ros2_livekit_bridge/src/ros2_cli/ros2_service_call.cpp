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

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"

#include <chrono>
#include <cstring>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rclcpp/client.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_runtime_c/service_type_support_struct.h>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

namespace introspection = rosidl_typesupport_introspection_cpp;
using introspection::MessageMember;
using introspection::MessageMembers;
using Clock = std::chrono::steady_clock;

constexpr auto kPollPeriod = std::chrono::milliseconds(2);
constexpr char kServiceTypeSupportSymbolPrefix[] =
  "__get_service_type_support_handle__";
constexpr char kRequestMessageTypeSuffix[] = "_Request";
constexpr char kResponseMessageTypeSuffix[] = "_Response";

namespace
{

/// @brief Construct a Ros2ServiceCall response.
Ros2ServiceCall::Response makeResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output = {})
{
  Ros2ServiceCall::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
}

/// @brief Load service type support by symbol from the typesupport library.
const rosidl_service_type_support_t * serviceTypeSupportHandle(
  const std::string & type,
  const std::string & typesupport_identifier,
  rcpputils::SharedLibrary & library)
{
  std::string symbol = typesupport_identifier + kServiceTypeSupportSymbolPrefix;
  for (const char ch : type) {
    if (ch == '/') {
      symbol += "__";
    } else {
      symbol += ch;
    }
  }
  if (!library.has_symbol(symbol)) {
    throw std::runtime_error("Service typesupport symbol not found: " + symbol);
  }
  using GetServiceTypeSupportHandleFn =
    const rosidl_service_type_support_t * (*)();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto get_handle = reinterpret_cast<GetServiceTypeSupportHandleFn>(
    library.get_symbol(symbol));
  return get_handle();
}

/// @brief Render a scalar response field as YAML-ish CLI text.
template<typename T>
void renderScalar(std::ostringstream & stream, const void * data)
{
  stream << *static_cast<const T *>(data);
}

/// @brief Render one introspection field value.
void renderField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory);

/// @brief Return pointer to a member inside a message buffer.
const void * memberMemory(const void * message, const MessageMember & member)
{
  return static_cast<const void *>(
    static_cast<const std::uint8_t *>(message) + member.offset_);
}

/// @brief Render one message as CLI-style YAML.
void renderMessage(
  std::ostringstream & stream,
  const MessageMembers & members,
  const void * message,
  std::size_t indent = 0U)
{
  const std::string padding(indent, ' ');
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto & member = members.members_[index];
    stream << padding << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member));
    stream << '\n';
  }
}

/// @brief Render a nested response message field.
void renderNestedMessage(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory)
{
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    stream << "{}";
    return;
  }
  stream << '\n';
  renderMessage(
    stream, *static_cast<const MessageMembers *>(member.members_->data),
    field_memory, 2U);
}

/// @brief Render one non-array scalar or nested field.
void renderSingleField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory)
{
  switch (member.type_id_) {
    case introspection::ROS_TYPE_FLOAT:
      renderScalar<float>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_DOUBLE:
      renderScalar<double>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_LONG_DOUBLE:
      renderScalar<long double>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_CHAR:
      stream << *static_cast<const char *>(field_memory);
      break;
    case introspection::ROS_TYPE_WCHAR:
      stream << static_cast<std::uint32_t>(
        *static_cast<const char16_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_BOOLEAN:
      stream << (*static_cast<const bool *>(field_memory) ? "true" : "false");
      break;
    case introspection::ROS_TYPE_OCTET:
    case introspection::ROS_TYPE_UINT8:
      stream << static_cast<unsigned>(
        *static_cast<const std::uint8_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_INT8:
      stream << static_cast<int>(
        *static_cast<const std::int8_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_UINT16:
      renderScalar<std::uint16_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT16:
      renderScalar<std::int16_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_UINT32:
      renderScalar<std::uint32_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT32:
      renderScalar<std::int32_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_UINT64:
      renderScalar<std::uint64_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT64:
      renderScalar<std::int64_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_STRING:
      stream << *static_cast<const std::string *>(field_memory);
      break;
    case introspection::ROS_TYPE_WSTRING:
      for (const auto code_unit :
        *static_cast<const std::u16string *>(field_memory))
      {
        stream << static_cast<char>(code_unit);
      }
      break;
    case introspection::ROS_TYPE_MESSAGE:
      renderNestedMessage(stream, member, field_memory);
      break;
    default:
      stream << "<unsupported ROS type id " << member.type_id_ << ">";
      break;
  }
}

/// @brief Render one array field as a compact YAML sequence.
void renderArrayField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory)
{
  const auto size = member.size_function == nullptr ?
    member.array_size_ : member.size_function(field_memory);
  stream << '[';
  for (std::size_t index = 0; index < size; ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    const auto * item = member.get_function(
      const_cast<void *>(field_memory), index);
    renderSingleField(stream, member, item);
  }
  stream << ']';
}

void renderField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory)
{
  if (member.is_array_) {
    renderArrayField(stream, member, field_memory);
    return;
  }
  renderSingleField(stream, member, field_memory);
}

/// @brief Format a runtime response message as CLI-style YAML.
std::string responseOutput(
  const MessageMembers & members,
  const void * response)
{
  std::ostringstream stream;
  renderMessage(stream, members, response);
  return stream.str();
}

}  // namespace

/// @brief Runtime type-support data for one ROS message type.
struct MessageTypeSupport
{
  /// @brief Load serialization and introspection type support for @p type.
  explicit MessageTypeSupport(const std::string & type)
  : serialization_library(rclcpp::get_typesupport_library(
        type, rosidl_typesupport_cpp::typesupport_identifier)),
    introspection_library(rclcpp::get_typesupport_library(
        type, introspection::typesupport_identifier)),
    serialization_handle(rclcpp::get_message_typesupport_handle(
        type, rosidl_typesupport_cpp::typesupport_identifier,
        *serialization_library)),
    introspection_handle(rclcpp::get_message_typesupport_handle(
        type, introspection::typesupport_identifier,
        *introspection_library)),
    members(requireMembers(introspection_handle)),
    serializer(serialization_handle)
  {}

  /// @brief Shared library that owns the serialization handle.
  std::shared_ptr<rcpputils::SharedLibrary> serialization_library;
  /// @brief Shared library that owns the introspection handle.
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  /// @brief C++ serialization type-support handle.
  const rosidl_message_type_support_t * serialization_handle;
  /// @brief C++ introspection type-support handle.
  const rosidl_message_type_support_t * introspection_handle;
  /// @brief Message member metadata from introspection type support.
  const MessageMembers & members;
  /// @brief Runtime serializer for this message type.
  rclcpp::SerializationBase serializer;

private:
  /// @brief Require message introspection data.
  static const MessageMembers & requireMembers(
    const rosidl_message_type_support_t * handle)
  {
    if (handle == nullptr || handle->data == nullptr) {
      throw std::runtime_error("Introspection type support handle is null");
    }
    return *static_cast<const MessageMembers *>(handle->data);
  }
};

/// @brief Runtime type-support data for one ROS service type.
struct ServiceTypeSupport
{
  /// @brief Load service, request, and response type support for @p type.
  explicit ServiceTypeSupport(const std::string & type)
  : library(rclcpp::get_typesupport_library(
        type, rosidl_typesupport_cpp::typesupport_identifier)),
    handle(serviceTypeSupportHandle(
        type, rosidl_typesupport_cpp::typesupport_identifier, *library)),
    request(type + kRequestMessageTypeSuffix),
    response(type + kResponseMessageTypeSuffix)
  {}

  /// @brief Shared library that owns the service handle.
  std::shared_ptr<rcpputils::SharedLibrary> library;
  /// @brief C++ service type-support handle.
  const rosidl_service_type_support_t * handle;
  /// @brief Request message type support.
  MessageTypeSupport request;
  /// @brief Response message type support.
  MessageTypeSupport response;
};

/// @brief Runtime service client for an arbitrary service type.
struct ServiceClient : public rclcpp::ClientBase
{
  /// @brief Construct a ClientBase-backed runtime service client.
  ServiceClient(
    const std::string & service_name,
    const std::string & interface_type,
    std::shared_ptr<ServiceTypeSupport> support,
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph)
  : rclcpp::ClientBase(node_base, std::move(node_graph)),
    interface_type(interface_type),
    support(std::move(support))
  {
    rcl_client_options_t options = rcl_client_get_default_options();
    const rcl_ret_t ret = rcl_client_init(
      get_client_handle().get(), get_rcl_node_handle(), this->support->handle,
      service_name.c_str(), &options);
    if (ret != RCL_RET_OK) {
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        rcl_reset_error();
        rclcpp::expand_topic_or_service_name(
          service_name, rcl_node_get_name(get_rcl_node_handle()),
          rcl_node_get_namespace(get_rcl_node_handle()), true);
      }
      rclcpp::exceptions::throw_from_rcl_error(
        ret, "could not create service client");
    }
  }

  /// @brief Create response storage for executor APIs.
  std::shared_ptr<void> create_response() override
  {
    struct ResponseStorage
    {
      /// @brief Keep type support alive beside response memory.
      explicit ResponseStorage(std::shared_ptr<ServiceTypeSupport> type_support)
      : support(std::move(type_support)),
        message(support->response.members,
          rosidl_runtime_cpp::MessageInitialization::ZERO)
      {}

      /// @brief Type support used to initialize message storage.
      std::shared_ptr<ServiceTypeSupport> support;
      /// @brief Runtime response message storage.
      DynamicMessage message;
    };

    auto storage = std::make_shared<ResponseStorage>(support);
    return std::shared_ptr<void>(storage, storage->message.data());
  }

  /// @brief Create response header storage for executor APIs.
  std::shared_ptr<rmw_request_id_t> create_request_header() override
  {
    return std::make_shared<rmw_request_id_t>();
  }

  /// @brief Executor response hook; direct polling consumes responses here.
  void handle_response(
    std::shared_ptr<rmw_request_id_t> request_header,
    std::shared_ptr<void> response) override
  {
    (void)request_header;
    (void)response;
  }

  /// @brief Service type used by this client.
  std::string interface_type;
  /// @brief Service, request, and response type support.
  std::shared_ptr<ServiceTypeSupport> support;
};

ServiceCaller::ServiceCaller(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph)
: base_(std::move(base)),
  graph_(std::move(graph))
{
  if (!base_ || !graph_) {
    throw std::invalid_argument(
      "ServiceCaller requires node base and graph interfaces");
  }
}

ServiceCaller::~ServiceCaller() = default;

Ros2ServiceCall::Response ServiceCaller::call(ServiceCallOptions options)
{
  std::string resolved_service;
  try {
    resolved_service = rclcpp::expand_topic_or_service_name(
      options.service, base_->get_name(), base_->get_namespace(), true);
  } catch (const std::exception & error) {
    return makeResponse(false, error.what());
  }

  if (options.interface_type.empty()) {
    return makeResponse(false, "interface_type must be non-empty");
  }
  if (options.request_payload.empty()) {
    return makeResponse(false, "request payload must be non-empty");
  }

  rclcpp::SerializedMessage serialized_request(options.request_payload.size());
  auto & raw_request = serialized_request.get_rcl_serialized_message();
  std::memcpy(
    raw_request.buffer, options.request_payload.data(),
    options.request_payload.size());
  raw_request.buffer_length = options.request_payload.size();

  const auto & interface_type = options.interface_type;

  ClientPtr client;
  try {
    client = getClient(resolved_service, interface_type);
  } catch (const std::exception & error) {
    return makeResponse(
      false, std::string("failed to create service client: ") +
               error.what());
  }

  DynamicMessage request_message(
    client->support->request.members,
    rosidl_runtime_cpp::MessageInitialization::ZERO);
  try {
    client->support->request.serializer.deserialize_message(
      &serialized_request, request_message.data());
  } catch (const std::exception & error) {
    return makeResponse(
      false, std::string("failed to build service request: ") +
               error.what());
  }

  std::int64_t sequence_number = 0;
  const rcl_ret_t send_ret = rcl_send_request(
    client->get_client_handle().get(), request_message.data(),
    &sequence_number);
  if (send_ret != RCL_RET_OK) {
    const std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    return makeResponse(false, "failed to send service request: " + message);
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

  return makeResponse(false, "Service call timed out.");
}

ServiceCaller::ClientPtr ServiceCaller::getClient(
  const std::string & service,
  const std::string & interface_type)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = service + ":" + interface_type;
  const auto existing = clients_.find(key);
  if (existing != clients_.end()) {
    return existing->second;
  }
  if (clients_.size() >= kMaxCachedServiceClients) {
    throw std::runtime_error("service client cache limit reached");
  }

  auto support = std::make_shared<ServiceTypeSupport>(interface_type);
  auto client = std::make_shared<ServiceClient>(
    service, interface_type, std::move(support), base_.get(), graph_);
  return clients_.emplace(key, std::move(client)).first->second;
}

std::optional<Ros2ServiceCall::Response> ServiceCaller::takeResponse(
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
      const auto output = responseOutput(
        client.support->response.members, response_message.data());
      return makeResponse(true, "", output);
    } catch (const std::exception & error) {
      return makeResponse(
        false, std::string("failed to convert service response: ") +
                 error.what());
    }
  }
}

}  // namespace ros2_livekit_bridge::ros2_cli
