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
#include <rclcpp/callback_group.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_logging_interface.hpp>
#include <rclcpp/node_interfaces/node_services_interface.hpp>
#include <rclcpp/service.hpp>
#include <string>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/runtime_service_type_support.hpp"
#include "ros2_livekit_bridge/types.hpp"

namespace ros2_livekit_bridge {

/// @brief Creates local ROS service servers that forward calls through LiveKit
/// RPC.
class ServiceForwarder {
public:
  /// @brief One configured service forwarding route.
  struct ServiceRoute {
    /// @brief Local and remote ROS service name.
    std::string service;
    /// @brief ROS service type, such as `std_srvs/srv/SetBool`.
    std::string msg_type;
    /// @brief LiveKit participant identity that hosts the remote service.
    std::string participant;
  };

  /// @brief LiveKit-facing callbacks needed by the forwarder.
  struct LiveKitMethods {
    /// @brief Check whether a remote participant exists.
    HasParticipantFn has_participant;
    /// @brief Perform one LiveKit RPC call.
    PerformRpcFn perform_rpc;
  };

  /// @brief ROS node interfaces required for dynamic service hosting.
  struct NodeInterfaces {
    /// @brief Node identity and shared RCL handle used when creating services.
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base;
    /// @brief Service registry used when creating ROS services.
    rclcpp::node_interfaces::NodeServicesInterface::SharedPtr node_services;
    /// @brief Logger used for diagnostics.
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging;
  };

  /// @brief Construct a service forwarder and create configured service
  /// servers.
  /// @param routes Service routes that should be exposed locally.
  /// @param node_interfaces Node interfaces used to create services.
  /// @param callback_group Callback group used by created services.
  /// @param livekit_methods LiveKit methods supplied by the bridge.
  /// @throws std::invalid_argument when required interfaces or callbacks are
  /// unset.
  ServiceForwarder(std::vector<ServiceRoute> routes, NodeInterfaces node_interfaces,
                   rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods);

  /// @brief Construct a service forwarder from a ROS node.
  /// @param routes Service routes that should be exposed locally.
  /// @param node Bridge node used for service hosting and logs.
  /// @param callback_group Callback group used by created services.
  /// @param livekit_methods LiveKit methods supplied by the bridge.
  ServiceForwarder(std::vector<ServiceRoute> routes, rclcpp::Node &node,
                   rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods);

  /// @brief Return the number of local service servers created.
  std::size_t serviceCount() const;

private:
  struct DynamicService;

  /// @brief Create one runtime-typed service server.
  void createService(const ServiceRoute &route, rclcpp::CallbackGroup::SharedPtr callback_group);

  /// @brief Forward one local ROS service request to the configured remote
  /// participant.
  /// @param route Route metadata for this service.
  /// @param support Runtime support for the forwarded service type.
  /// @param request_data Runtime-typed request message memory.
  /// @param response_data Runtime-typed response message memory to populate.
  void forwardRequest(const ServiceRoute &route, const ros2_cli::RuntimeServiceTypeSupport &support,
                      const void *request_data, void *response_data) const;

  NodeInterfaces node_interfaces_;
  rclcpp::Logger logger_;
  std::vector<rclcpp::ServiceBase::SharedPtr> services_;
  LiveKitMethods livekit_methods_;
};

} // namespace ros2_livekit_bridge
