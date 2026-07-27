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

#include "ros2_livekit_bridge/cli/manager.hpp"

#include <algorithm>
#include <array>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/cli/constants.hpp"
#include "ros2_livekit_bridge/cli/interface_show.hpp"
#include "ros2_livekit_bridge/cli/json_converters.hpp"
#include "ros2_livekit_bridge/cli/service_list.hpp"
#include "ros2_livekit_bridge/cli/topic_list.hpp"
#include "ros2_livekit_bridge/cli/utils.hpp"

namespace ros2_livekit_bridge::cli {

namespace {

/// Diagnostic task name for the CLI manager status.
constexpr char kCliManagerDiagnosticTaskName[] = "cli_manager";

/// Create a ROS service, returning nullptr (and logging) instead of throwing so
/// a single failed service does not abort manager construction.
template <typename SrvT, typename CallbackT>
typename rclcpp::Service<SrvT>::SharedPtr tryCreateService(const Manager::NodeInterfaces& node_interfaces,
                                                           const char* service_name, CallbackT callback,
                                                           const rclcpp::CallbackGroup::SharedPtr& callback_group) {
  try {
    return rclcpp::create_service<SrvT>(node_interfaces.node_base, node_interfaces.node_services, service_name,
                                        std::move(callback), rclcpp::ServicesQoS(), callback_group);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(node_interfaces.node_logging->get_logger(), "Failed to create ROS service '%s': %s", service_name,
                 error.what());
    return nullptr;
  }
}

} // namespace

Manager::Manager(NodeInterfaces node_interfaces, rclcpp::CallbackGroup::SharedPtr callback_group,
                 LiveKitMethods livekit_methods, TopicPublishAllowed topic_publish_allowed,
                 diagnostics::DiagnosticsManagerFns diagnostics)
    : node_interfaces_(std::move(node_interfaces)),
      livekit_methods_(std::move(livekit_methods)),
      topic_publish_allowed_(std::move(topic_publish_allowed)),
      diagnostics_(std::move(diagnostics)) {
  if (!node_interfaces_.node_base || !node_interfaces_.node_services || !node_interfaces_.node_graph ||
      !node_interfaces_.node_topics || !node_interfaces_.node_logging) {
    throw std::invalid_argument("Manager requires fully populated NodeInterfaces");
  }

  if (!livekit_methods_.has_participant || !livekit_methods_.perform_rpc || !livekit_methods_.register_rpc_method ||
      !livekit_methods_.unregister_rpc_method) {
    throw std::invalid_argument("Manager requires fully populated LiveKitMethods");
  }

  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("Manager requires fully populated DiagnosticsManagerFns");
  }

  if (!topic_publish_allowed_) {
    topic_publish_allowed_ = [](const std::string&) { return true; };
  }

  topic_publisher_ =
      std::make_unique<TopicPub>(node_interfaces_.node_topics, node_interfaces_.node_graph, topic_publish_allowed_);
  service_caller_ = std::make_unique<ServiceCall>(node_interfaces_.node_base, node_interfaces_.node_graph,
                                                  node_interfaces_.node_logging->get_logger());

  // Service and RPC creation are best-effort: on failure we log and continue so
  // the bridge stays up in a degraded state, and the diagnostic task reports
  // which command pairs are incomplete.
  topic_list_service_ = tryCreateService<TopicListSrv>(
      node_interfaces_, kTopicListServiceName,
      [this](const std::shared_ptr<TopicListSrv::Request> request, std::shared_ptr<TopicListSrv::Response> response) {
        handleTopicListRosService(request, response);
      },
      callback_group);

  topic_pub_service_ = tryCreateService<TopicPubSrv>(
      node_interfaces_, kTopicPubServiceName,
      [this](const std::shared_ptr<TopicPubSrv::Request> request, std::shared_ptr<TopicPubSrv::Response> response) {
        handleTopicPubRosService(request, response);
      },
      callback_group);

  service_list_service_ = tryCreateService<ServiceListSrv>(
      node_interfaces_, kServiceListServiceName,
      [this](const std::shared_ptr<ServiceListSrv::Request> request,
             std::shared_ptr<ServiceListSrv::Response> response) { handleServiceListRosService(request, response); },
      callback_group);

  service_call_service_ = tryCreateService<ServiceCallSrv>(
      node_interfaces_, kServiceCallServiceName,
      [this](const std::shared_ptr<ServiceCallSrv::Request> request,
             std::shared_ptr<ServiceCallSrv::Response> response) { handleServiceCallRosService(request, response); },
      callback_group);

  interface_show_service_ = tryCreateService<InterfaceShowSrv>(
      node_interfaces_, kInterfaceShowServiceName,
      [this](const std::shared_ptr<InterfaceShowSrv::Request> request,
             std::shared_ptr<InterfaceShowSrv::Response> response) {
        handleInterfaceShowRosService(request, response);
      },
      callback_group);

  const auto register_rpc = [this](const char* method, auto handler) {
    if (livekit_methods_.register_rpc_method(method, std::move(handler))) {
      registered_rpc_methods_.emplace_back(method);
    } else {
      RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to register LiveKit RPC method '%s'", method);
    }
  };

  register_rpc(kTopicListRpcMethod, [this](const std::string& payload) { return handleTopicListRpc(payload); });
  register_rpc(kTopicPubRpcMethod, [this](const std::string& payload) { return handleTopicPubRpc(payload); });
  register_rpc(kServiceListRpcMethod, [this](const std::string& payload) { return handleServiceListRpc(payload); });
  register_rpc(kServiceCallRpcMethod, [this](const std::string& payload) { return handleServiceCallRpc(payload); });
  register_rpc(kInterfaceShowRpcMethod, [this](const std::string& payload) { return handleInterfaceShowRpc(payload); });

  RCLCPP_INFO(node_interfaces_.node_logging->get_logger(),
              "CLI manager initialized: %d/5 ROS services, %zu/5 RPC methods",
              (topic_list_service_ != nullptr) + (topic_pub_service_ != nullptr) + (service_list_service_ != nullptr) +
                  (service_call_service_ != nullptr) + (interface_show_service_ != nullptr),
              registered_rpc_methods_.size());

  diagnostics_.add(kCliManagerDiagnosticTaskName,
                   [this](diagnostic_updater::DiagnosticStatusWrapper& status) { populateStatus(status); });
}

Manager::Manager(rclcpp::Node& node, rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods,
                 TopicPublishAllowed topic_publish_allowed, diagnostics::DiagnosticsManagerFns diagnostics)
    : Manager(
          NodeInterfaces{
              node.get_node_base_interface(),
              node.get_node_services_interface(),
              node.get_node_graph_interface(),
              node.get_node_topics_interface(),
              node.get_node_logging_interface(),
          },
          callback_group, std::move(livekit_methods), std::move(topic_publish_allowed), std::move(diagnostics)) {}

Manager::~Manager() {
  // Deregister the diagnostic task before the state it reads is torn down.
  diagnostics_.remove(kCliManagerDiagnosticTaskName);
  if (livekit_methods_.unregister_rpc_method) {
    for (const auto& method : registered_rpc_methods_) {
      livekit_methods_.unregister_rpc_method(method);
    }
  }
}

bool Manager::rpcRegistered(const std::string& rpc_method) const {
  return std::find(registered_rpc_methods_.begin(), registered_rpc_methods_.end(), rpc_method) !=
         registered_rpc_methods_.end();
}

void Manager::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) const {
  struct CommandStatus {
    const char* key;
    bool service_created;
    bool rpc_registered;
  };

  const std::array<CommandStatus, 5> commands{{
      {kTopicListRpcMethod, topic_list_service_ != nullptr, rpcRegistered(kTopicListRpcMethod)},
      {kTopicPubRpcMethod, topic_pub_service_ != nullptr, rpcRegistered(kTopicPubRpcMethod)},
      {kServiceListRpcMethod, service_list_service_ != nullptr, rpcRegistered(kServiceListRpcMethod)},
      {kServiceCallRpcMethod, service_call_service_ != nullptr, rpcRegistered(kServiceCallRpcMethod)},
      {kInterfaceShowRpcMethod, interface_show_service_ != nullptr, rpcRegistered(kInterfaceShowRpcMethod)},
  }};

  std::size_t complete_pairs = 0;
  for (const auto& command : commands) {
    const bool complete = command.service_created && command.rpc_registered;
    const char* detail = nullptr;
    if (complete) {
      ++complete_pairs;
      detail = "ok";
    } else if (!command.service_created && !command.rpc_registered) {
      detail = "service and rpc missing";
    } else if (!command.service_created) {
      detail = "service missing";
    } else {
      detail = "rpc missing";
    }
    status.add(command.key, detail);
  }

  // Cache pressure: the bounded generic-publisher and service-client caches
  // reject (they do not evict) new entries once full, so a nonzero rejection
  // count means requests were dropped for lack of a cache slot.
  const CacheStats topic_pub_cache = topic_publisher_ ? topic_publisher_->cacheStats() : CacheStats{};
  const CacheStats service_call_cache = service_caller_ ? service_caller_->cacheStats() : CacheStats{};
  status.add("topic_pub_cache", std::to_string(topic_pub_cache.size) + "/" + std::to_string(topic_pub_cache.capacity));
  status.add("topic_pub_cache_full_rejections", std::to_string(topic_pub_cache.cache_full_rejections));
  status.add("service_call_cache",
             std::to_string(service_call_cache.size) + "/" + std::to_string(service_call_cache.capacity));
  status.add("service_call_cache_full_rejections", std::to_string(service_call_cache.cache_full_rejections));

  // Remote-call failure breakdown (cumulative). Informational: these reflect
  // per-request outcomes (e.g. a caller passing a bad participant id), not
  // manager health, so they do not by themselves change the diagnostic level.
  status.add("remote_participant_not_found", std::to_string(remote_participant_not_found_.load()));
  status.add("remote_transport_failures", std::to_string(remote_transport_failures_.load()));
  status.add("remote_malformed_responses", std::to_string(remote_malformed_responses_.load()));

  const bool cache_pressure = topic_pub_cache.cache_full_rejections > 0 || service_call_cache.cache_full_rejections > 0;

  if (complete_pairs != commands.size()) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "One or more CLI command pairs failed to register");
  } else if (cache_pressure) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN,
                   "CLI command pairs registered; publisher/client cache is dropping requests");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "All CLI command pairs registered");
  }
}

void Manager::handleTopicListRosService(const std::shared_ptr<TopicListSrv::Request> request,
                                        std::shared_ptr<TopicListSrv::Response> response) const {
  *response = callRemoteTopicList(*request);
}

void Manager::handleTopicPubRosService(const std::shared_ptr<TopicPubSrv::Request> request,
                                       std::shared_ptr<TopicPubSrv::Response> response) const {
  *response = callRemoteTopicPub(*request);
}

void Manager::handleServiceListRosService(const std::shared_ptr<ServiceListSrv::Request> request,
                                          std::shared_ptr<ServiceListSrv::Response> response) const {
  *response = callRemoteServiceList(*request);
}

void Manager::handleServiceCallRosService(const std::shared_ptr<ServiceCallSrv::Request> request,
                                          std::shared_ptr<ServiceCallSrv::Response> response) const {
  *response = callRemoteServiceCall(*request);
}

void Manager::handleInterfaceShowRosService(const std::shared_ptr<InterfaceShowSrv::Request> request,
                                            std::shared_ptr<InterfaceShowSrv::Response> response) const {
  *response = callRemoteInterfaceShow(*request);
}

template <typename ResponseT>
ResponseT Manager::performRemoteRpc(const std::string& participant_id, const char* rpc_method,
                                    const std::string& request_payload, std::uint8_t timeout_sec) const {
  const auto rpc_response = livekit_methods_.perform_rpc(participant_id, rpc_method, request_payload, timeout_sec);
  if (!rpc_response) {
    ++remote_transport_failures_;
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "LiveKit RPC '%s' to participant '%s' failed", rpc_method,
                 participant_id.c_str());
    return makeCliResponse<ResponseT>(false, std::string("remote ") + rpc_method + " RPC failed");
  }

  std::string parse_error;
  auto response = cliResponseFromJson<ResponseT>(*rpc_response, parse_error);
  if (!response) {
    ++remote_malformed_responses_;
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "LiveKit RPC '%s' from participant '%s' returned malformed JSON: %s", rpc_method,
                 participant_id.c_str(), parse_error.c_str());
    return makeCliResponse<ResponseT>(false, std::string("remote ") + rpc_method + " returned malformed JSON");
  }
  return response.value();
}

TopicListSrv::Response Manager::callRemoteTopicList(const TopicListSrv::Request& request) const {
  if (request.participant_id.empty()) {
    return makeCliResponse<TopicListSrv::Response>(false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    ++remote_participant_not_found_;
    return makeCliResponse<TopicListSrv::Response>(
        false, "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicListRequestToJson(request, timeout_sec);
  return performRemoteRpc<TopicListSrv::Response>(request.participant_id, kTopicListRpcMethod, payload, timeout_sec);
}

TopicPubSrv::Response Manager::callRemoteTopicPub(const TopicPubSrv::Request& request) const {
  if (request.participant_id.empty()) {
    return makeCliResponse<TopicPubSrv::Response>(false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    ++remote_participant_not_found_;
    return makeCliResponse<TopicPubSrv::Response>(false,
                                                  "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicPubRequestToJson(request, timeout_sec);

  const auto options = topicPubOptionsFromJson(payload);
  if (!options) {
    return makeCliResponse<TopicPubSrv::Response>(false, options.error());
  }

  return performRemoteRpc<TopicPubSrv::Response>(request.participant_id, kTopicPubRpcMethod, payload, timeout_sec);
}

ServiceListSrv::Response Manager::callRemoteServiceList(const ServiceListSrv::Request& request) const {
  if (request.participant_id.empty()) {
    return makeCliResponse<ServiceListSrv::Response>(false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    ++remote_participant_not_found_;
    return makeCliResponse<ServiceListSrv::Response>(
        false, "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = serviceListRequestToJson(request, timeout_sec);
  return performRemoteRpc<ServiceListSrv::Response>(request.participant_id, kServiceListRpcMethod, payload,
                                                    timeout_sec);
}

ServiceCallSrv::Response Manager::callRemoteServiceCall(const ServiceCallSrv::Request& request) const {
  if (request.participant_id.empty()) {
    return makeCliResponse<ServiceCallSrv::Response>(false, "participant_id must be non-empty");
  }

  if (request.service.empty()) {
    return makeCliResponse<ServiceCallSrv::Response>(false, "service must be non-empty");
  }

  if (request.msg_type.empty()) {
    return makeCliResponse<ServiceCallSrv::Response>(false, "msg_type must be non-empty");
  }

  if (request.payload.empty()) {
    return makeCliResponse<ServiceCallSrv::Response>(false, "payload must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    ++remote_participant_not_found_;
    return makeCliResponse<ServiceCallSrv::Response>(
        false, "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto service_timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto rpc_timeout_sec = serviceCallRpcTimeout(service_timeout_sec);
  const auto payload = serviceCallRequestToJson(request, service_timeout_sec);

  return performRemoteRpc<ServiceCallSrv::Response>(request.participant_id, kServiceCallRpcMethod, payload,
                                                    rpc_timeout_sec);
}

InterfaceShowSrv::Response Manager::callRemoteInterfaceShow(const InterfaceShowSrv::Request& request) const {
  if (request.participant_id.empty()) {
    return makeCliResponse<InterfaceShowSrv::Response>(false, "participant_id must be non-empty");
  }

  if (request.all_comments && request.no_comments) {
    return makeCliResponse<InterfaceShowSrv::Response>(false, "all_comments and no_comments are mutually exclusive");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    ++remote_participant_not_found_;
    return makeCliResponse<InterfaceShowSrv::Response>(
        false, "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = interfaceShowRequestToJson(request, timeout_sec);
  return performRemoteRpc<InterfaceShowSrv::Response>(request.participant_id, kInterfaceShowRpcMethod, payload,
                                                      timeout_sec);
}

std::string Manager::handleTopicListRpc(const std::string& payload) const {
  const auto options = topicListOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kTopicListRpcMethod, options.error().c_str());
    return cliResponseToJson(false, options.error(), "");
  }

  try {
    const auto output =
        formatTopicList(collectTopicInfo(*node_interfaces_.node_graph, options.value()), options.value());
    return cliResponseToJson(true, "", output);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kTopicListRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string Manager::handleTopicPubRpc(const std::string& payload) const {
  const auto options = topicPubOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kTopicPubRpcMethod, options.error().c_str());
    return cliResponseToJson(false, options.error(), "");
  }

  try {
    const auto response = topic_publisher_->publish(options.value());
    return cliResponseToJson(response.success, response.err_msg, response.output);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kTopicPubRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string Manager::handleServiceListRpc(const std::string& payload) const {
  const auto options = serviceListOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kServiceListRpcMethod, options.error().c_str());
    return cliResponseToJson(false, options.error(), "");
  }

  try {
    const auto output =
        formatServiceList(collectServiceInfo(*node_interfaces_.node_graph, options.value()), options.value());
    return cliResponseToJson(true, "", output);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kServiceListRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string Manager::handleServiceCallRpc(const std::string& payload) const {
  std::string options_error;
  const auto options = serviceCallOptionsFromJson(payload, options_error);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kServiceCallRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  const auto response = service_caller_->call(*options);
  return cliResponseToJson(response.success, response.err_msg, response.output);
}

std::string Manager::handleInterfaceShowRpc(const std::string& payload) const {
  const auto options = interfaceShowOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kInterfaceShowRpcMethod, options.error().c_str());
    return cliResponseToJson(false, options.error(), "");
  }

  try {
    const auto output = renderInterfaceDefinition(options.value());
    if (!output.has_value()) {
      std::string error_message;
      if (options.value().type.empty()) {
        error_message = "the passed value is empty";
      } else if (options.value().type == "-") {
        error_message = "expected stdin pipe";
      } else if (options.value().all_comments && options.value().no_comments) {
        error_message = "all_comments and no_comments are mutually exclusive";
      } else {
        error_message = "Could not find interface '" + options.value().type + "'";
      }
      RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                   kInterfaceShowRpcMethod, error_message.c_str());
      return cliResponseToJson(false, error_message, "");
    }
    return cliResponseToJson(true, "", *output);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kInterfaceShowRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

bool Manager::isHiddenTopic(const std::string& topic_name) { return hasHiddenNameToken(topic_name); }

bool Manager::isHiddenService(const std::string& service_name) { return hasHiddenNameToken(service_name); }

std::uint8_t Manager::effectiveTimeout(std::uint8_t timeout_sec) {
  return timeout_sec == 0 ? kDefaultTimeoutSec : timeout_sec;
}

std::uint8_t Manager::serviceCallRpcTimeout(std::uint8_t service_timeout_sec) {
  const unsigned total = static_cast<unsigned>(service_timeout_sec) + kServiceCallRpcTimeoutMarginSec;
  return static_cast<std::uint8_t>(std::min(total, 255U));
}

} // namespace ros2_livekit_bridge::cli
