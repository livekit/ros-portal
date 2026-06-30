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

#include <rcl/client.h>
#include <rcl/error_handling.h>
#include <rosidl_runtime_c/service_type_support_struct.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/client.hpp>
#include <rclcpp/exceptions.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <rcpputils/shared_library.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "ros2_livekit_bridge/ros2_cli/types.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros2_livekit_bridge::ros2_cli {

/// @brief Implements the ROS-side behavior for one-shot `ros2 service call`.
///
/// The manager owns transport concerns. This class owns command behavior:
/// resolving service names, serializing the native YAML request into the
/// request type, dispatching a runtime-typed service client, and formatting the
/// response for CLI-style consumers.
class Ros2ServiceCall {
public:
  /// @brief Construct a runtime-typed service caller helper.
  /// @param base Node base interface used for RCL client construction.
  /// @param graph Node graph interface used by rclcpp client base.
  /// @param logger Logger used for service-call diagnostics.
  /// @throws std::invalid_argument if either required node interface is null.
  Ros2ServiceCall(rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
                  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph, rclcpp::Logger logger);

  /// @brief Release cached runtime service clients.
  ~Ros2ServiceCall();

  Ros2ServiceCall(const Ros2ServiceCall &) = delete;
  Ros2ServiceCall &operator=(const Ros2ServiceCall &) = delete;

  /// @brief Call one ROS service using runtime service type support.
  ///
  /// Resolves the service name in the bridge node context, serializes the
  /// native YAML payload into the runtime request message type, sends the
  /// request, waits for a matching response or timeout, and returns a ROS
  /// service response.
  ///
  /// @param options Service name, required type, YAML request payload, and
  /// timeout.
  /// @return Service-call response with success false and err_msg filled when
  ///   validation, request conversion, client creation, dispatch, timeout, or
  ///   response conversion fails.
  Ros2ServiceCallSrv::Response call(ServiceCallOptions options);

#ifdef BUILD_TESTING
  /// @brief Return the service type-support creation error for @p type.
  static std::string serviceTypeSupportCreationError(const std::string &type);
#endif

private:
  /// @brief Build the service type-support symbol name for @p type.
  ///
  /// Replaces `/` characters in @p type with `__` when appending to the
  /// type-support prefix.
  ///
  /// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
  /// @param typesupport_identifier Type-support identifier to build the symbol.
  /// @return Symbol name to resolve in the type-support shared library.
  static std::string serviceTypeSupportSymbol(const std::string &type, const std::string &typesupport_identifier);

  /// @brief Load service type support by symbol from the typesupport library.
  ///
  /// Implementation detail of the service-call command, used by the runtime
  /// type-support loader. Static so it can be exercised without an instance.
  ///
  /// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
  /// @param typesupport_identifier Type-support identifier to build the symbol.
  /// @param library Loaded type-support shared library to resolve the symbol
  /// in.
  /// @return Service type-support handle for @p type, or nullptr when the
  ///   type-support symbol is not found.
  static const rosidl_service_type_support_t *serviceTypeSupportHandle(const std::string &type,
                                                                       const std::string &typesupport_identifier,
                                                                       rcpputils::SharedLibrary &library);

  /// @brief Runtime type-support data for one ROS message type.
  ///
  /// Defined in the translation unit; forward-declared here because it is an
  /// implementation detail of the service-call command.
  struct MessageTypeSupport;

  /// @brief Runtime type-support data for one ROS service type.
  ///
  /// Defined in the translation unit; forward-declared here because it is an
  /// implementation detail of the service-call command.
  struct ServiceTypeSupport;

  /// @brief Runtime service client for an arbitrary service type.
  ///
  /// Defined in the translation unit; forward-declared here because it is an
  /// implementation detail of the service-call command.
  struct ServiceClient;

  /// @brief Service client shared pointer type.
  using ClientPtr = std::shared_ptr<ServiceClient>;

  /// @brief Return a cached client, creating it if needed.
  /// @param service Resolved ROS service name.
  /// @param msg_type Required service type identifier.
  /// @param error Populated with a failure reason when client creation fails.
  /// @return Cached or newly created runtime service client, or std::nullopt
  ///   when type support or client construction fails.
  std::optional<ClientPtr> getClient(const std::string &service, const std::string &msg_type, std::string &error);

  /// @brief Take and convert a matching service response when available.
  /// @param client Runtime service client to poll for a response.
  /// @param sequence_number Sequence number of the dispatched request.
  /// @return Service-call response when a matching response is taken, or
  ///   std::nullopt when no matching response is currently available.
  std::optional<Ros2ServiceCallSrv::Response> takeResponse(ServiceClient &client, std::int64_t sequence_number);

  /// @brief Node base interface used for RCL client construction.
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base_;
  /// @brief Node graph interface used by rclcpp client base.
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph_;
  /// @brief Logger used for service-call diagnostics.
  rclcpp::Logger logger_;
  /// @brief Guards the runtime service client cache.
  std::mutex mutex_;
  /// @brief Bounded service client cache keyed by resolved service and type.
  std::unordered_map<std::string, ClientPtr> clients_;

#ifdef BUILD_TESTING
  FRIEND_TEST(Ros2ServiceCallPrivateTest, ServiceTypeSupportSymbolReplacesSlashes);
  FRIEND_TEST(Ros2ServiceCallPrivateTest, ServiceTypeSupportHandleLoadsKnownService);
  FRIEND_TEST(Ros2ServiceCallPrivateTest, ServiceTypeSupportHandleReturnsNullForMissingSymbol);
#endif
};

} // namespace ros2_livekit_bridge::ros2_cli
