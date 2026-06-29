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

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rclcpp/client.hpp>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_runtime_c/service_type_support_struct.h>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"
#include "ros2_livekit_bridge/ros2_cli/types.hpp"

namespace ros2_livekit_bridge::ros2_cli
{

/// @brief Implements the ROS-side behavior for one-shot `ros2 service call`.
///
/// The manager owns transport concerns. This class owns command behavior:
/// resolving service names, serializing the native YAML request into the
/// request type, dispatching a runtime-typed service client, and formatting the
/// response for CLI-style consumers.
class Ros2ServiceCall
{
public:
  /// @brief Construct a runtime-typed service caller helper.
  /// @param base Node base interface used for RCL client construction.
  /// @param graph Node graph interface used by rclcpp client base.
  /// @throws std::invalid_argument if either required node interface is null.
  Ros2ServiceCall(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph);

  /// @brief Release cached runtime service clients.
  ~Ros2ServiceCall();

  Ros2ServiceCall(const Ros2ServiceCall &) = delete;
  Ros2ServiceCall & operator=(const Ros2ServiceCall &) = delete;

  /// @brief Call one ROS service using runtime service type support.
  ///
  /// Resolves the service name in the bridge node context, serializes the native
  /// YAML payload into the runtime request message type, sends the request, waits
  /// for a matching response or timeout, and returns a ROS service response.
  ///
  /// @param options Service name, required type, YAML request payload, and timeout.
  /// @return Service-call response with success false and err_msg filled when
  ///   validation, request conversion, client creation, dispatch, timeout, or
  ///   response conversion fails.
  Ros2ServiceCallSrv::Response call(ServiceCallOptions options);

private:
  /// @brief Introspection type-support namespace alias.
  using MessageMembers =
    rosidl_typesupport_introspection_cpp::MessageMembers;

  /// @brief Load service type support by symbol from the typesupport library.
  ///
  /// Implementation detail of the service-call command, used by the runtime
  /// type-support loader. Static so it can be exercised without an instance.
  ///
  /// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
  /// @param typesupport_identifier Type-support identifier to build the symbol.
  /// @param library Loaded type-support shared library to resolve the symbol in.
  /// @return Service type-support handle for @p type.
  /// @throws std::runtime_error if the type-support symbol is not found.
  static const rosidl_service_type_support_t * serviceTypeSupportHandle(
    const std::string & type,
    const std::string & typesupport_identifier,
    rcpputils::SharedLibrary & library);

  /// @brief Runtime type-support data for one ROS message type.
  struct MessageTypeSupport
  {
    /// @brief Load serialization and introspection type support for @p type.
    explicit MessageTypeSupport(const std::string & type)
    : serialization_library(rclcpp::get_typesupport_library(
          type, rosidl_typesupport_cpp::typesupport_identifier)),
      introspection_library(rclcpp::get_typesupport_library(
          type, rosidl_typesupport_introspection_cpp::typesupport_identifier)),
      serialization_handle(rclcpp::get_message_typesupport_handle(
          type, rosidl_typesupport_cpp::typesupport_identifier,
          *serialization_library)),
      introspection_handle(rclcpp::get_message_typesupport_handle(
          type, rosidl_typesupport_introspection_cpp::typesupport_identifier,
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

private:
    /// @brief Request message type-name suffix.
    static constexpr char kRequestMessageTypeSuffix[] = "_Request";
    /// @brief Response message type-name suffix.
    static constexpr char kResponseMessageTypeSuffix[] = "_Response";
  };

  /// @brief Runtime service client for an arbitrary service type.
  struct ServiceClient : public rclcpp::ClientBase
  {
    /// @brief Construct a ClientBase-backed runtime service client.
    ServiceClient(
      const std::string & service_name,
      const std::string & msg_type,
      std::shared_ptr<ServiceTypeSupport> support,
      rclcpp::node_interfaces::NodeBaseInterface * node_base,
      rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph)
    : rclcpp::ClientBase(node_base, std::move(node_graph)),
      msg_type(msg_type),
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
        explicit ResponseStorage(
          std::shared_ptr<ServiceTypeSupport> type_support)
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
    std::string msg_type;
    /// @brief Service, request, and response type support.
    std::shared_ptr<ServiceTypeSupport> support;
    /// @brief Serializes send+take on this client across concurrent callers.
    std::mutex call_mutex;
  };

  /// @brief Service client shared pointer type.
  using ClientPtr = std::shared_ptr<ServiceClient>;

  /// @brief Return a cached client, creating it if needed.
  /// @param service Resolved ROS service name.
  /// @param msg_type Required service type identifier.
  /// @return Cached or newly created runtime service client.
  /// @throws std::runtime_error if the client cache limit is reached.
  /// @throws std::exception if type support or client construction fails.
  ClientPtr getClient(
    const std::string & service,
    const std::string & msg_type);

  /// @brief Take and convert a matching service response when available.
  /// @param client Runtime service client to poll for a response.
  /// @param sequence_number Sequence number of the dispatched request.
  /// @return Service-call response when a matching response is taken, or
  ///   std::nullopt when no matching response is currently available.
  std::optional<Ros2ServiceCallSrv::Response> takeResponse(
    ServiceClient & client,
    std::int64_t sequence_number);

  /// @brief Node base interface used for RCL client construction.
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base_;
  /// @brief Node graph interface used by rclcpp client base.
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph_;
  /// @brief Guards the runtime service client cache.
  std::mutex mutex_;
  /// @brief Bounded service client cache keyed by resolved service and type.
  std::unordered_map<std::string, ClientPtr> clients_;
};

}  // namespace ros2_livekit_bridge::ros2_cli
