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
#include <string>
#include <unordered_map>

#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>

#include "ros2_livekit_bridge/ros2_cli/types.hpp"

namespace ros2_livekit_bridge::ros2_cli
{

/// @brief Runtime service client for an arbitrary service type (defined in the source).
struct ServiceClient;

/// @brief Implements the ROS-side behavior for one-shot `ros2 service call`.
///
/// The manager owns transport concerns. This class owns command behavior:
/// resolving service names, serializing the native YAML request into the
/// request type, dispatching a runtime-typed service client, and formatting the
/// response for CLI-style consumers.
class ServiceCaller
{
public:
  /// @brief Construct a runtime-typed service caller helper.
  /// @param base Node base interface used for RCL client construction.
  /// @param graph Node graph interface used by rclcpp client base.
  /// @throws std::invalid_argument if either required node interface is null.
  ServiceCaller(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base,
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph);

  /// @brief Release cached runtime service clients.
  ~ServiceCaller();

  ServiceCaller(const ServiceCaller &) = delete;
  ServiceCaller & operator=(const ServiceCaller &) = delete;

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
  Ros2ServiceCall::Response call(ServiceCallOptions options);

private:
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
  std::optional<Ros2ServiceCall::Response> takeResponse(
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
