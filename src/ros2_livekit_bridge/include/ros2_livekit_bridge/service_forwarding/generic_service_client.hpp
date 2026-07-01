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

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <rclcpp/logger.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <string>
#include <vector>

#include "ros2_livekit_bridge/service_type_support.hpp"

namespace ros2_livekit_bridge {

/// @brief A type-erased ROS service client that exchanges serialized CDR.
///
/// Wraps an `rclcpp::ClientBase` initialized via `rcl_client_init` for an
/// arbitrary service type. Like @ref ros2_cli::Ros2ServiceCall it polls
/// `take_type_erased_response` directly rather than relying on executor
/// dispatch, so @ref call is safe to invoke from a non-executor thread (such as
/// the LiveKit room event thread); the executor still services the client's
/// wait set on another thread.
class GenericServiceClient {
public:
  /// @brief Construct and initialize the rcl client handle.
  /// @param service_name Resolved ROS service name.
  /// @param support Runtime service type support for request/response.
  /// @param node_base Node base interface used for RCL client construction.
  /// @param node_graph Node graph interface used by rclcpp ClientBase.
  /// @param logger Logger for diagnostics.
  /// @throws rclcpp::exceptions::RCLError when the rcl client cannot be created.
  GenericServiceClient(const std::string &service_name, std::shared_ptr<ServiceTypeSupport> support,
                       rclcpp::node_interfaces::NodeBaseInterface *node_base,
                       rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph, rclcpp::Logger logger);

  ~GenericServiceClient();

  GenericServiceClient(const GenericServiceClient &) = delete;
  GenericServiceClient &operator=(const GenericServiceClient &) = delete;

  /// @brief Whether a matching service server is currently available.
  bool serviceIsReady() const;

  /// @brief Send a serialized CDR request and wait for the CDR response.
  /// @param request_cdr Serialized request bytes.
  /// @param timeout Maximum time to wait for a matching response.
  /// @return Serialized response bytes, or std::nullopt on request
  ///   deserialization failure, send failure, or timeout.
  std::optional<std::vector<std::uint8_t>> call(const std::vector<std::uint8_t> &request_cdr,
                                                std::chrono::nanoseconds timeout);

private:
  /// @brief Runtime service client; defined in the translation unit.
  struct Client;

  /// @brief Take and serialize a matching response when available.
  std::optional<std::vector<std::uint8_t>> takeResponse(std::int64_t sequence_number);

  /// @brief Runtime request/response type support.
  std::shared_ptr<ServiceTypeSupport> support_;
  /// @brief Logger used for client diagnostics.
  rclcpp::Logger logger_;
  /// @brief ClientBase-backed runtime client.
  std::shared_ptr<Client> client_;
};

} // namespace ros2_livekit_bridge
