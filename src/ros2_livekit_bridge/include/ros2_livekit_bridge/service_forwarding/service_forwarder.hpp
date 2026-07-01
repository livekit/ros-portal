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
#include <rclcpp/callback_group.hpp>
#include <rclcpp/clock.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/node.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/types.hpp"

namespace ros2_livekit_bridge_config {
struct BridgeConfig;
} // namespace ros2_livekit_bridge_config

namespace ros2_livekit_bridge {

class GenericService;
class GenericServiceClient;

/// @brief Forwards ROS2 service calls to LiveKit RPC and vice versa.
///
/// Mirrors @ref TopicForwarder for the request/response case. For each
/// configured service it sets up, eagerly at construction:
/// - "out": a local proxy ROS service server (@ref GenericService)
///   that serializes inbound requests to CDR and forwards them to a remote
///   participant via a LiveKit RPC, then returns the CDR response.
/// - "in": a LiveKit RPC method that, when invoked, calls the local ROS service
///   through a @ref GenericServiceClient and returns its CDR response.
/// - "bidirectional": both of the above for the same entry.
///
/// The forwarder owns no `livekit::Room`; it reuses the bridge's RPC callbacks.
class ServiceForwarder {
public:
  /// @brief Direction a configured service is forwarded.
  enum class Direction { In, Out, Bidirectional };

  /// @brief One configured service forwarding entry.
  struct ServiceForwarderEntry {
    /// @brief ROS service name, e.g. "/set_bool".
    std::string service;
    /// @brief ROS service type, e.g. "std_srvs/srv/SetBool".
    std::string msg_type;
    /// @brief Forwarding direction.
    Direction direction{Direction::Out};
    /// @brief Remote LiveKit participant identity for the RPC.
    std::string participant;
  };

  /// @brief Service forwarding options derived from bridge configuration.
  struct ServiceForwarderOptions {
    /// @brief Configured services to forward.
    std::vector<ServiceForwarderEntry> services;
    /// @brief Service-call timeout in seconds (zero is treated as default).
    std::uint8_t service_call_timeout_sec{ros2_cli::kDefaultTimeoutSec};
  };

  /// @brief LiveKit-facing callbacks supplied by the bridge.
  ///
  /// Same shape as @c Ros2CliManager::LivekitMethods; the bridge populates each
  /// from its own room so the forwarder owns no LiveKit room reference.
  struct LiveKitMethods {
    HasParticipantFn has_participant;
    PerformRpcFn perform_rpc;
    RegisterRpcMethodFn register_rpc_method;
    UnregisterRpcMethodFn unregister_rpc_method;
  };

  /// @brief Construct a service forwarder and set up all configured entries.
  /// @param options Forwarding configuration.
  /// @param node Non-owning handle to the ROS node used to create the proxy
  ///   servers and clients.
  /// @param livekit_methods LiveKit RPC callbacks supplied by the bridge.
  /// @throws std::invalid_argument when the node has expired or any required
  ///   LiveKit callback is unset.
  ServiceForwarder(ServiceForwarderOptions options, rclcpp::Node::WeakPtr node, LiveKitMethods livekit_methods);

  /// @brief Unregister inbound RPC methods and tear down proxy servers.
  ~ServiceForwarder();

  ServiceForwarder(const ServiceForwarder &) = delete;
  ServiceForwarder &operator=(const ServiceForwarder &) = delete;

  /// @brief Derive the deterministic, <=64-byte LiveKit RPC method name for a
  /// service.
  ///
  /// Both the out-side caller and the in-side handler compute the same name
  /// purely from the service name, so the two ends agree without negotiation.
  /// Long names fall back to a stable hash suffix to stay within the LiveKit
  /// method-name byte limit.
  /// @param service ROS service name.
  /// @return RPC method name.
  static std::string rpcMethodName(const std::string &service);

  /// @brief Resolve the LiveKit RPC timeout for a given service-call timeout.
  ///
  /// The RPC round-trip must outlive the remote ROS service-call wait, so this
  /// adds a small margin (saturating at 255 seconds).
  /// @param service_timeout_sec Service-call timeout in seconds.
  /// @return LiveKit RPC timeout in seconds.
  static std::uint8_t rpcTimeout(std::uint8_t service_timeout_sec);

  /// @brief Map configured services into ServiceForwarder entries.
  /// @param config Parsed bridge configuration.
  /// @return One entry per configured service, with the config direction mapped
  ///   to @ref Direction.
  static std::vector<ServiceForwarderEntry> entriesFromConfig(const ros2_livekit_bridge_config::BridgeConfig &config);

private:
  /// @brief Per-service outbound state; defined in the translation unit.
  struct OutboundServiceState;
  /// @brief Per-service inbound state; defined in the translation unit.
  struct InboundServiceState;

  /// @brief Create and register the outbound proxy server for @p entry.
  void setupOutbound(rclcpp::Node &node, const ServiceForwarderEntry &entry);
  /// @brief Register the inbound RPC handler for @p entry.
  void setupInbound(const ServiceForwarderEntry &entry);

  /// @brief Outbound server callback: forward a CDR request via LiveKit RPC.
  std::optional<std::vector<std::uint8_t>> forwardOutboundCall(const OutboundServiceState &state,
                                                               const std::vector<std::uint8_t> &request_cdr);
  /// @brief Inbound RPC handler: call the local ROS service for @p payload.
  std::string handleInboundRpc(const std::shared_ptr<InboundServiceState> &state, const std::string &payload);

  /// @brief Forwarding configuration supplied at construction.
  ServiceForwarderOptions options_;
  /// @brief Non-owning handle to the ROS node.
  rclcpp::Node::WeakPtr node_;
  /// @brief LiveKit RPC callbacks supplied by the bridge.
  LiveKitMethods livekit_methods_;
  /// @brief Reentrant callback group for outbound proxy servers.
  rclcpp::CallbackGroup::SharedPtr callback_group_;
  /// @brief Logger borrowed from the ROS node.
  rclcpp::Logger logger_;
  /// @brief Clock used for throttled logging.
  rclcpp::Clock::SharedPtr clock_;

  /// @brief Protects the outbound proxy server map.
  std::mutex outbound_mutex_;
  /// @brief Outbound proxy servers keyed by ROS service name.
  std::unordered_map<std::string, std::shared_ptr<OutboundServiceState>> outbound_services_;
  /// @brief Protects the inbound handler map.
  std::mutex inbound_mutex_;
  /// @brief Inbound handlers keyed by RPC method name.
  std::unordered_map<std::string, std::shared_ptr<InboundServiceState>> inbound_services_;
};

} // namespace ros2_livekit_bridge
