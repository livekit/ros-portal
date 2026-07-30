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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <rclcpp/callback_group.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_logging_interface.hpp>
#include <rclcpp/node_interfaces/node_services_interface.hpp>
#include <rclcpp/service.hpp>
#include <string>
#include <vector>

#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/introspection/runtime_type_support.hpp"
#include "ros_portal/types.hpp"

namespace diagnostic_updater {
class DiagnosticStatusWrapper;
}

namespace ros_portal {

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
    /// @brief Return whether the local room session can perform RPC work.
    IsRoomAvailableFn is_room_available;
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
  /// @param livekit_methods LiveKit methods supplied by ROS Portal.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register
  /// the service-forwarder diagnostic task.
  /// @throws std::invalid_argument when required interfaces or callbacks are
  /// unset.
  ServiceForwarder(const std::vector<ServiceRoute>& routes, NodeInterfaces node_interfaces,
                   const rclcpp::CallbackGroup::SharedPtr& callback_group, LiveKitMethods livekit_methods,
                   diagnostics::DiagnosticsManagerFns diagnostics);

  /// @brief Construct a service forwarder from a ROS node.
  /// @param routes Service routes that should be exposed locally.
  /// @param node ROS Portal node used for service hosting and logs.
  /// @param callback_group Callback group used by created services.
  /// @param livekit_methods LiveKit methods supplied by ROS Portal.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register
  /// the service-forwarder diagnostic task.
  ServiceForwarder(const std::vector<ServiceRoute>& routes, rclcpp::Node& node,
                   const rclcpp::CallbackGroup::SharedPtr& callback_group, LiveKitMethods livekit_methods,
                   diagnostics::DiagnosticsManagerFns diagnostics);

  /// @brief Deregister the service-forwarder diagnostic task.
  ~ServiceForwarder();

  /// @brief Return the number of local service servers created.
  std::size_t serviceCount() const;

private:
  struct DynamicService;

  /// @brief Mutable counters and metadata published by the diagnostic task.
  struct DiagnosticState {
    /// @brief Construct diagnostic state for the configured route count.
    /// @param configured_routes Number of outgoing service routes.
    explicit DiagnosticState(std::size_t configured_routes) : routes_configured(configured_routes) {}

    /// @brief Number of configured outgoing service routes.
    std::size_t routes_configured{0U};
    /// @brief Count of routes rejected due to incomplete configuration.
    std::atomic<std::uint64_t> routes_skipped_invalid_config{0};
    /// @brief Count of routes whose runtime type support could not be loaded.
    std::atomic<std::uint64_t> routes_skipped_no_type_support{0};
    /// @brief Count of local requests handled by the forwarder.
    std::atomic<std::uint64_t> requests_forwarded{0};
    /// @brief Count of requests completed with a populated local response.
    std::atomic<std::uint64_t> requests_succeeded{0};
    /// @brief Count of requests that failed during forwarding.
    std::atomic<std::uint64_t> requests_failed{0};
    /// @brief Count of requests whose target participant was unavailable.
    std::atomic<std::uint64_t> failures_participant_not_found{0};
    /// @brief Count of requests whose LiveKit RPC failed.
    std::atomic<std::uint64_t> failures_rpc_transport{0};
    /// @brief Count of malformed LiveKit RPC responses.
    std::atomic<std::uint64_t> failures_malformed_response{0};
    /// @brief Count of errors returned by the remote service.
    std::atomic<std::uint64_t> failures_remote_error{0};
    /// @brief Count of requests that could not be serialized.
    std::atomic<std::uint64_t> failures_request_serialization{0};
    /// @brief Count of responses that could not populate the ROS response.
    std::atomic<std::uint64_t> failures_response_deserialization{0};
    /// @brief Count of exceptions caught around forwarding handlers.
    std::atomic<std::uint64_t> handler_exceptions{0};
    /// @brief Count of timeouts sending responses to local ROS clients.
    std::atomic<std::uint64_t> response_send_timeouts{0};
    /// @brief Protects the most recent failure metadata.
    std::mutex last_failure_mutex;
    /// @brief Service associated with the most recent failure.
    std::string last_failure_service;
    /// @brief Stable category associated with the most recent failure.
    std::string last_failure_reason;
  };

  /// @brief Create one runtime-typed service server.
  /// @param route Service route metadata.
  /// @param callback_group Callback group used by the created service.
  void createService(const ServiceRoute& route, const rclcpp::CallbackGroup::SharedPtr& callback_group);

  /// @brief Forward one local ROS service request to the configured remote
  /// participant.
  /// @param route Route metadata for this service.
  /// @param request_data Runtime-typed request message memory.
  /// @param response_data Runtime-typed response message memory to populate.
  void forwardRequest(const ServiceRoute& route, const void* request_data, void* response_data);

  /// @brief Record one failed forwarded request and its route.
  void recordRequestFailure(const ServiceRoute& route, const std::string& reason);
  /// @brief Record an exception escaping a forwarding handler.
  void recordHandlerException(const std::string& service);
  /// @brief Record a timeout sending a response to the local ROS client.
  void recordResponseSendTimeout(const std::string& service);
  /// @brief Update the most recent failure metadata.
  void recordLastFailure(const std::string& service, const std::string& reason);
  /// @brief Populate the service-forwarder diagnostic status.
  void populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status);

  NodeInterfaces node_interfaces_;
  LiveKitMethods livekit_methods_;
  diagnostics::DiagnosticsManagerFns diagnostics_;
  rclcpp::Logger logger_;
  std::vector<rclcpp::ServiceBase::SharedPtr> services_;
  /// @brief Mutable state owned exclusively for diagnostics reporting.
  DiagnosticState diagnostic_state_;
};

} // namespace ros_portal
