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

#include "ros2_livekit_bridge/service_forwarder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/logging.hpp>
#include <rclcpp/service.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/generic_service.hpp"
#include "ros2_livekit_bridge/generic_service_client.hpp"
#include "ros2_livekit_bridge/service_type_support.hpp"
#include "ros2_livekit_bridge/service_rpc_codec.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"

namespace ros2_livekit_bridge {

namespace {

/// @brief LiveKit RPC method names are capped at 64 bytes of UTF-8.
constexpr std::size_t kMaxRpcMethodNameBytes = 64;
/// @brief Stable RPC method-name prefix for forwarded services.
constexpr char kServiceRpcMethodPrefix[] = "ros2_srv:";

/// @brief 32-bit FNV-1a hash rendered as 8 lowercase hex characters.
std::string fnv1aHex(const std::string &value) {
  std::uint32_t hash = 2166136261U;
  for (const unsigned char ch : value) {
    hash ^= ch;
    hash *= 16777619U;
  }
  std::array<char, 9> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%08x", hash);
  return std::string(buffer.data());
}

} // namespace

struct ServiceForwarder::OutboundServiceState {
  ServiceForwarderEntry entry;
  std::string rpc_method;
  std::shared_ptr<GenericService> server;
};

struct ServiceForwarder::InboundServiceState {
  ServiceForwarderEntry entry;
  std::string rpc_method;
  /// @brief Serializes lazy client creation and calls.
  std::mutex client_mutex;
  /// @brief Lazily created on the first inbound request.
  std::unique_ptr<GenericServiceClient> client;
};

ServiceForwarder::ServiceForwarder(ServiceForwarderOptions options, rclcpp::Node::WeakPtr node,
                                   LiveKitMethods livekit_methods)
    : options_(std::move(options)),
      node_(std::move(node)),
      livekit_methods_(std::move(livekit_methods)),
      logger_(rclcpp::get_logger("service_forwarder")) {
  const auto locked_node = node_.lock();
  if (!locked_node) {
    throw std::invalid_argument("ServiceForwarder requires a non-expired ROS node");
  }

  if (!livekit_methods_.has_participant || !livekit_methods_.perform_rpc || !livekit_methods_.register_rpc_method ||
      !livekit_methods_.unregister_rpc_method) {
    throw std::invalid_argument("ServiceForwarder requires fully populated LiveKitMethods");
  }

  logger_ = locked_node->get_logger().get_child("service_forwarder");
  clock_ = locked_node->get_clock();
  callback_group_ = locked_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  for (const auto &entry : options_.services) {
    const bool do_out = entry.direction == Direction::Out || entry.direction == Direction::Bidirectional;
    const bool do_in = entry.direction == Direction::In || entry.direction == Direction::Bidirectional;
    if (do_out) {
      setupOutbound(*locked_node, entry);
    }
    if (do_in) {
      setupInbound(entry);
    }
    if (entry.direction == Direction::Bidirectional) {
      RCLCPP_WARN(logger_,
                  "Service '%s' is bidirectional: the local out-proxy server and the in-handler target share this "
                  "name on one node and can self-loop. Use distinct service names per node.",
                  entry.service.c_str());
    }
  }
}

ServiceForwarder::~ServiceForwarder() {
  // Unregister inbound RPC methods so the SDK stops invoking handlers while the
  // local participant is still available.
  {
    std::lock_guard<std::mutex> lock(inbound_mutex_);
    if (livekit_methods_.unregister_rpc_method) {
      for (const auto &entry : inbound_services_) {
        livekit_methods_.unregister_rpc_method(entry.first);
      }
    }
    inbound_services_.clear();
  }

  // Destroy outbound proxy servers (rcl_service_fini runs in the handle
  // deleter). Relies on the executor having stopped before destruction.
  {
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_services_.clear();
  }
}

std::string ServiceForwarder::rpcMethodName(const std::string &service) {
  std::string stripped = service;
  if (!stripped.empty() && stripped.front() == '/') {
    stripped.erase(0, 1);
  }
  const std::string token = utils::sanitizeRosNameToken(stripped).value_or("");

  std::string candidate = std::string(kServiceRpcMethodPrefix) + token;
  if (!token.empty() && candidate.size() <= kMaxRpcMethodNameBytes) {
    return candidate;
  }

  // Too long or empty after sanitizing: append a stable hash of the full
  // original service name and truncate the readable token to fit. The hash is
  // computed identically on both ends so the method name still matches.
  const std::string hash = fnv1aHex(service);
  const std::size_t prefix_len = std::string(kServiceRpcMethodPrefix).size();
  const std::size_t suffix_len = hash.size() + 1U; // '_' + hash
  const std::size_t max_token =
      kMaxRpcMethodNameBytes > prefix_len + suffix_len ? kMaxRpcMethodNameBytes - prefix_len - suffix_len : 0U;
  std::string trimmed = token.size() > max_token ? token.substr(0, max_token) : token;
  return std::string(kServiceRpcMethodPrefix) + trimmed + "_" + hash;
}

std::uint8_t ServiceForwarder::rpcTimeout(std::uint8_t service_timeout_sec) {
  const unsigned total = static_cast<unsigned>(service_timeout_sec) + ros2_cli::kServiceCallRpcTimeoutMarginSec;
  return static_cast<std::uint8_t>(std::min(total, 255U));
}

void ServiceForwarder::setupOutbound(rclcpp::Node &node, const ServiceForwarderEntry &entry) {
  std::string ts_error;
  auto support = ServiceTypeSupport::create(entry.msg_type, ts_error);
  if (!support) {
    RCLCPP_ERROR(logger_, "Skipping outbound service '%s': failed to load type support for '%s': %s",
                 entry.service.c_str(), entry.msg_type.c_str(), ts_error.c_str());
    return;
  }

  auto state = std::make_shared<OutboundServiceState>();
  state->entry = entry;
  state->rpc_method = rpcMethodName(entry.service);

  GenericService::RequestCallback callback =
      [this, state](std::vector<std::uint8_t> request_cdr) -> std::optional<std::vector<std::uint8_t>> {
    return forwardOutboundCall(*state, request_cdr);
  };

  try {
    auto server = std::make_shared<GenericService>(node.get_node_base_interface()->get_shared_rcl_node_handle(),
                                                   entry.service, std::move(support), std::move(callback));
    node.get_node_services_interface()->add_service(std::static_pointer_cast<rclcpp::ServiceBase>(server),
                                                    callback_group_);
    state->server = std::move(server);
  } catch (const std::exception &error) {
    RCLCPP_ERROR(logger_, "Skipping outbound service '%s': %s", entry.service.c_str(), error.what());
    return;
  }

  const std::string method = state->rpc_method;
  {
    std::lock_guard<std::mutex> lock(outbound_mutex_);
    outbound_services_[entry.service] = std::move(state);
  }
  RCLCPP_INFO(logger_, "Forwarding outbound ROS service '%s' (%s) to participant '%s' via RPC method '%s'",
              entry.service.c_str(), entry.msg_type.c_str(), entry.participant.c_str(), method.c_str());
}

void ServiceForwarder::setupInbound(const ServiceForwarderEntry &entry) {
  auto state = std::make_shared<InboundServiceState>();
  state->entry = entry;
  state->rpc_method = rpcMethodName(entry.service);

  const bool registered = livekit_methods_.register_rpc_method(
      state->rpc_method, [this, state](const std::string &payload) { return handleInboundRpc(state, payload); });
  if (!registered) {
    RCLCPP_ERROR(logger_, "Failed to register RPC method '%s' for inbound service '%s'", state->rpc_method.c_str(),
                 entry.service.c_str());
    return;
  }

  const std::string method = state->rpc_method;
  {
    std::lock_guard<std::mutex> lock(inbound_mutex_);
    inbound_services_[method] = std::move(state);
  }
  RCLCPP_INFO(logger_, "Forwarding inbound RPC method '%s' to local ROS service '%s' (%s)", method.c_str(),
              entry.service.c_str(), entry.msg_type.c_str());
}

std::optional<std::vector<std::uint8_t>> ServiceForwarder::forwardOutboundCall(
    const OutboundServiceState &state, const std::vector<std::uint8_t> &request_cdr) {
  const std::string &participant = state.entry.participant;
  if (!livekit_methods_.has_participant(participant)) {
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Outbound service '%s': participant '%s' not found",
                         state.entry.service.c_str(), participant.c_str());
    return std::nullopt;
  }

  const std::string payload = encodeServiceRequest(request_cdr);
  if (payload.size() > kMaxRpcPayloadBytes) {
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Outbound service '%s': request payload %zu B exceeds %zu B limit",
                         state.entry.service.c_str(), payload.size(), kMaxRpcPayloadBytes);
    return std::nullopt;
  }

  const std::uint8_t rpc_timeout_sec = rpcTimeout(options_.service_call_timeout_sec);
  const auto rpc_response = livekit_methods_.perform_rpc(participant, state.rpc_method, payload, rpc_timeout_sec);
  if (!rpc_response) {
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Outbound service '%s': RPC to participant '%s' failed",
                         state.entry.service.c_str(), participant.c_str());
    return std::nullopt;
  }

  auto decoded = decodeServiceResponse(*rpc_response);
  if (!decoded.ok) {
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Outbound service '%s': remote returned error: %s",
                         state.entry.service.c_str(), decoded.err.c_str());
    return std::nullopt;
  }
  return std::move(decoded.response_cdr);
}

std::string ServiceForwarder::handleInboundRpc(const std::shared_ptr<InboundServiceState> &state,
                                               const std::string &payload) {
  auto request_cdr = decodeServiceRequest(payload);
  if (!request_cdr) {
    return encodeServiceResponse(false, {}, "invalid base64 request payload");
  }

  std::lock_guard<std::mutex> lock(state->client_mutex);
  if (!state->client) {
    const auto node = node_.lock();
    if (!node) {
      return encodeServiceResponse(false, {}, "ROS node is unavailable");
    }
    std::string ts_error;
    auto support = ServiceTypeSupport::create(state->entry.msg_type, ts_error);
    if (!support) {
      return encodeServiceResponse(false, {}, "failed to load type support: " + ts_error);
    }
    try {
      state->client = std::make_unique<GenericServiceClient>(
          state->entry.service, std::move(support), node->get_node_base_interface().get(),
          node->get_node_graph_interface(), node->get_logger().get_child("service_forwarder"));
    } catch (const std::exception &error) {
      return encodeServiceResponse(false, {}, std::string("failed to create service client: ") + error.what());
    }
  }

  if (!state->client->serviceIsReady()) {
    return encodeServiceResponse(false, {}, "local service '" + state->entry.service + "' is not available");
  }

  const std::uint8_t timeout_sec =
      options_.service_call_timeout_sec == 0 ? ros2_cli::kDefaultTimeoutSec : options_.service_call_timeout_sec;
  auto response_cdr = state->client->call(*request_cdr, std::chrono::seconds(timeout_sec));
  if (!response_cdr) {
    return encodeServiceResponse(false, {}, "local service call failed or timed out");
  }

  std::string response_payload = encodeServiceResponse(true, *response_cdr, "");
  if (response_payload.size() > kMaxRpcPayloadBytes) {
    return encodeServiceResponse(false, {}, "response payload exceeds RPC size limit");
  }
  return response_payload;
}

} // namespace ros2_livekit_bridge
