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

#include "ros2_livekit_bridge/ros2_cli_manager.hpp"
#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_interface_show.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_service_call.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_service_list.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_topic_list.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace ros2_livekit_bridge
{

Ros2CliManager::Ros2CliManager(
  NodeInterfaces node_interfaces,
  rclcpp::CallbackGroup::SharedPtr callback_group,
  LivekitMethods livekit_methods,
  TopicPublishAllowed topic_publish_allowed)
: node_interfaces_(std::move(node_interfaces)),
  livekit_methods_(std::move(livekit_methods)),
  topic_publish_allowed_(std::move(topic_publish_allowed))
{
  if (!node_interfaces_.node_base || !node_interfaces_.node_services ||
    !node_interfaces_.node_graph || !node_interfaces_.node_topics ||
    !node_interfaces_.node_logging)
  {
    throw std::invalid_argument(
        "Ros2CliManager requires fully populated NodeInterfaces");
  }

  if (!livekit_methods_.has_participant || !livekit_methods_.perform_rpc ||
    !livekit_methods_.register_rpc_method ||
    !livekit_methods_.unregister_rpc_method)
  {
    throw std::invalid_argument(
        "Ros2CliManager requires fully populated LivekitMethods");
  }

  if (!topic_publish_allowed_) {
    topic_publish_allowed_ = [](const std::string &) {return true;};
  }

  topic_publisher_ = std::make_unique<ros2_cli::Ros2TopicPub>(
    node_interfaces_.node_topics, node_interfaces_.node_graph,
    topic_publish_allowed_);
  service_caller_ = std::make_unique<ros2_cli::ServiceCaller>(
    node_interfaces_.node_base, node_interfaces_.node_graph);

  topic_list_service_ = rclcpp::create_service<Ros2TopicList>(
      node_interfaces_.node_base, node_interfaces_.node_services,
      ros2_cli::kTopicListServiceName,
    [this](const std::shared_ptr<Ros2TopicList::Request> request,
    std::shared_ptr<Ros2TopicList::Response> response) {
      handleTopicListRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  topic_pub_service_ = rclcpp::create_service<Ros2TopicPubSrv>(
      node_interfaces_.node_base, node_interfaces_.node_services,
      ros2_cli::kTopicPubServiceName,
    [this](const std::shared_ptr<Ros2TopicPubSrv::Request> request,
    std::shared_ptr<Ros2TopicPubSrv::Response> response) {
      handleTopicPubRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  service_list_service_ = rclcpp::create_service<Ros2ServiceList>(
      node_interfaces_.node_base, node_interfaces_.node_services,
      ros2_cli::kServiceListServiceName,
    [this](const std::shared_ptr<Ros2ServiceList::Request> request,
    std::shared_ptr<Ros2ServiceList::Response> response) {
      handleServiceListRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  service_call_service_ = rclcpp::create_service<Ros2ServiceCall>(
      node_interfaces_.node_base, node_interfaces_.node_services,
      ros2_cli::kServiceCallServiceName,
    [this](const std::shared_ptr<Ros2ServiceCall::Request> request,
    std::shared_ptr<Ros2ServiceCall::Response> response) {
      handleServiceCallRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  interface_show_service_ = rclcpp::create_service<Ros2InterfaceShow>(
      node_interfaces_.node_base, node_interfaces_.node_services,
      ros2_cli::kInterfaceShowServiceName,
    [this](const std::shared_ptr<Ros2InterfaceShow::Request> request,
    std::shared_ptr<Ros2InterfaceShow::Response> response) {
      handleInterfaceShowRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  livekit_methods_.register_rpc_method(ros2_cli::kTopicListRpcMethod,
    [this](const std::string & payload) {
      return handleTopicListRpc(payload);
                                       });

  livekit_methods_.register_rpc_method(ros2_cli::kTopicPubRpcMethod,
    [this](const std::string & payload) {
      return handleTopicPubRpc(payload);
                                       });

  livekit_methods_.register_rpc_method(ros2_cli::kServiceListRpcMethod,
    [this](const std::string & payload) {
      return handleServiceListRpc(payload);
                                       });

  livekit_methods_.register_rpc_method(ros2_cli::kServiceCallRpcMethod,
    [this](const std::string & payload) {
      return handleServiceCallRpc(payload);
                                       });

  livekit_methods_.register_rpc_method(ros2_cli::kInterfaceShowRpcMethod,
    [this](const std::string & payload) {
      return handleInterfaceShowRpc(payload);
                                       });

  RCLCPP_INFO(
      node_interfaces_.node_logging->get_logger(),
      "Registered ROS services:\n   - %s\n   - %s\n   - %s\n   - %s",
      ros2_cli::kTopicListServiceName, ros2_cli::kTopicPubServiceName,
      ros2_cli::kServiceListServiceName, ros2_cli::kInterfaceShowServiceName);
  RCLCPP_INFO(
      node_interfaces_.node_logging->get_logger(),
      "Registered LiveKit RPC methods:\n   - %s\n   - %s\n   - %s\n   - %s",
      ros2_cli::kTopicListRpcMethod, ros2_cli::kTopicPubRpcMethod,
      ros2_cli::kServiceListRpcMethod, ros2_cli::kServiceCallRpcMethod,
      ros2_cli::kInterfaceShowRpcMethod);
}

Ros2CliManager::Ros2CliManager(
  rclcpp::Node & node,
  rclcpp::CallbackGroup::SharedPtr callback_group,
  LivekitMethods livekit_methods,
  TopicPublishAllowed topic_publish_allowed)
: Ros2CliManager(
    NodeInterfaces{
    node.get_node_base_interface(),
    node.get_node_services_interface(),
    node.get_node_graph_interface(),
    node.get_node_topics_interface(),
    node.get_node_logging_interface(),
  },
    callback_group, std::move(livekit_methods),
    std::move(topic_publish_allowed)) {}

Ros2CliManager::~Ros2CliManager()
{
  if (livekit_methods_.unregister_rpc_method) {
    livekit_methods_.unregister_rpc_method(ros2_cli::kTopicListRpcMethod);
    livekit_methods_.unregister_rpc_method(ros2_cli::kTopicPubRpcMethod);
    livekit_methods_.unregister_rpc_method(ros2_cli::kServiceListRpcMethod);
    livekit_methods_.unregister_rpc_method(ros2_cli::kServiceCallRpcMethod);
    livekit_methods_.unregister_rpc_method(ros2_cli::kInterfaceShowRpcMethod);
  }
}

void Ros2CliManager::handleTopicListRosService(
  const std::shared_ptr<Ros2TopicList::Request> request,
  std::shared_ptr<Ros2TopicList::Response> response) const
{
  *response = callRemoteTopicList(*request);
}

void Ros2CliManager::handleTopicPubRosService(
  const std::shared_ptr<Ros2TopicPubSrv::Request> request,
  std::shared_ptr<Ros2TopicPubSrv::Response> response) const
{
  *response = callRemoteTopicPub(*request);
}

void Ros2CliManager::handleServiceListRosService(
  const std::shared_ptr<Ros2ServiceList::Request> request,
  std::shared_ptr<Ros2ServiceList::Response> response) const
{
  *response = callRemoteServiceList(*request);
}

void Ros2CliManager::handleServiceCallRosService(
  const std::shared_ptr<Ros2ServiceCall::Request> request,
  std::shared_ptr<Ros2ServiceCall::Response> response) const
{
  *response = callRemoteServiceCall(*request);
}

void Ros2CliManager::handleInterfaceShowRosService(
  const std::shared_ptr<Ros2InterfaceShow::Request> request,
  std::shared_ptr<Ros2InterfaceShow::Response> response) const
{
  *response = callRemoteInterfaceShow(*request);
}

template <typename ResponseT>
ResponseT Ros2CliManager::performRemoteRpc(
  const std::string & participant_id,
  const char * rpc_method,
  const std::string & request_payload,
  std::uint8_t timeout_sec) const
{
  const auto rpc_response = livekit_methods_.perform_rpc(
      participant_id, rpc_method, request_payload, timeout_sec);
  if (!rpc_response) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "LiveKit RPC '%s' to participant '%s' failed",
                 rpc_method, participant_id.c_str());
    return makeCliResponse<ResponseT>(
      false, std::string("remote ") + rpc_method + " RPC failed");
  }

  std::string parse_error;
  auto response = cliResponseFromJson<ResponseT>(*rpc_response, parse_error);
  if (!response) {
    RCLCPP_ERROR(
        node_interfaces_.node_logging->get_logger(),
        "LiveKit RPC '%s' from participant '%s' returned malformed JSON: %s",
        rpc_method, participant_id.c_str(), parse_error.c_str());
    return makeCliResponse<ResponseT>(
      false, std::string("remote ") + rpc_method + " returned malformed JSON");
  }
  return response.value();
}

Ros2CliManager::Ros2TopicPubSrv::Response Ros2CliManager::callRemoteTopicList(
  const Ros2TopicList::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeCliResponse<Ros2TopicList::Response>(
      false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    return makeCliResponse<Ros2TopicList::Response>(
      false, "LiveKit participant '" + request.participant_id +
               "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicListRequestToJson(request, timeout_sec);
  return performRemoteRpc<Ros2TopicList::Response>(
    request.participant_id, ros2_cli::kTopicListRpcMethod, payload, timeout_sec);
}

Ros2CliManager::Ros2TopicPub::Response
Ros2CliManager::callRemoteTopicPub(const Ros2TopicPub::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeCliResponse<Ros2TopicPub::Response>(
      false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    return makeCliResponse<Ros2TopicPub::Response>(
      false, "LiveKit participant '" + request.participant_id +
               "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicPubRequestToJson(request, timeout_sec);

  std::string options_error;
  if (!topicPubOptionsFromJson(payload, options_error)) {
    return makeCliResponse<Ros2TopicPub::Response>(false, options_error);
  }

  return performRemoteRpc<Ros2TopicPub::Response>(
    request.participant_id, ros2_cli::kTopicPubRpcMethod, payload, timeout_sec);
}

Ros2CliManager::Ros2ServiceList::Response Ros2CliManager::callRemoteServiceList(
  const Ros2ServiceList::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeCliResponse<Ros2ServiceList::Response>(
      false, "participant_id must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    return makeCliResponse<Ros2ServiceList::Response>(
      false, "LiveKit participant '" + request.participant_id +
               "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = serviceListRequestToJson(request, timeout_sec);
  return performRemoteRpc<Ros2ServiceList::Response>(
    request.participant_id, ros2_cli::kServiceListRpcMethod, payload,
    timeout_sec);
}

Ros2CliManager::Ros2ServiceCall::Response
Ros2CliManager::callRemoteServiceCall(
  const Ros2ServiceCall::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeCliResponse<Ros2ServiceCall::Response>(
      false, "participant_id must be non-empty");
  }

  if (request.service.empty()) {
    return makeCliResponse<Ros2ServiceCall::Response>(
      false, "service must be non-empty");
  }

  if (request.interface_type.empty()) {
    return makeCliResponse<Ros2ServiceCall::Response>(
      false, "interface_type must be non-empty");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    return makeCliResponse<Ros2ServiceCall::Response>(
      false, "LiveKit participant '" + request.participant_id +
               "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  std::string payload_error;
  const auto payload = serviceCallRequestToJson(
    request, timeout_sec, payload_error);
  if (!payload) {
    return makeCliResponse<Ros2ServiceCall::Response>(
      false, "failed to build service request: " + payload_error);
  }

  return performRemoteRpc<Ros2ServiceCall::Response>(
    request.participant_id, ros2_cli::kServiceCallRpcMethod, *payload,
    timeout_sec);
}

Ros2CliManager::Ros2InterfaceShow::Response
Ros2CliManager::callRemoteInterfaceShow(
  const Ros2InterfaceShow::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeCliResponse<Ros2InterfaceShow::Response>(
      false, "participant_id must be non-empty");
  }

  if (request.all_comments && request.no_comments) {
    return makeCliResponse<Ros2InterfaceShow::Response>(
      false, "all_comments and no_comments are mutually exclusive");
  }

  if (!livekit_methods_.has_participant(request.participant_id)) {
    return makeCliResponse<Ros2InterfaceShow::Response>(
      false, "LiveKit participant '" + request.participant_id +
               "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = interfaceShowRequestToJson(request, timeout_sec);
  return performRemoteRpc<Ros2InterfaceShow::Response>(
    request.participant_id, ros2_cli::kInterfaceShowRpcMethod, payload,
    timeout_sec);
}

std::string
Ros2CliManager::handleTopicListRpc(const std::string & payload) const
{
  const auto options = topicListOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kTopicListRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  try {
    const auto output = ros2_cli::formatTopicList(
        ros2_cli::collectTopicInfo(*node_interfaces_.node_graph, *options),
        *options);
    return cliResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kTopicListRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string
Ros2CliManager::handleTopicPubRpc(const std::string & payload) const
{
  const auto options = topicPubOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kTopicPubRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  try {
    const auto response = topic_publisher_->publish(*options);
    return cliResponseToJson(response.success, response.err_msg,
                                  response.output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kTopicPubRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string
Ros2CliManager::handleServiceListRpc(const std::string & payload) const
{
  const auto options = serviceListOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kServiceListRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  try {
    const auto output = ros2_cli::formatServiceList(
        ros2_cli::collectServiceInfo(*node_interfaces_.node_graph, *options),
        *options);
    return cliResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kServiceListRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

std::string
Ros2CliManager::handleServiceCallRpc(const std::string & payload) const
{
  std::string options_error;
  const auto options = serviceCallOptionsFromJson(payload, options_error);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kServiceCallRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  const auto response = service_caller_->call(*options);
  return cliResponseToJson(
    response.success, response.err_msg, response.output);
}

std::string
Ros2CliManager::handleInterfaceShowRpc(const std::string & payload) const
{
  const auto options = interfaceShowOptionsFromJson(payload);
  if (!options) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kInterfaceShowRpcMethod, options_error.c_str());
    return cliResponseToJson(false, options_error, "");
  }

  try {
    const auto output = ros2_cli::renderInterfaceDefinition(options.value());
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
      RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                   "Failed to handle LiveKit RPC '%s': %s",
                   ros2_cli::kInterfaceShowRpcMethod, error_message.c_str());
      return cliResponseToJson(false, error_message, "");
    }
    return cliResponseToJson(true, "", *output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_interfaces_.node_logging->get_logger(),
                 "Failed to handle LiveKit RPC '%s': %s",
                 ros2_cli::kInterfaceShowRpcMethod, error.what());
    return cliResponseToJson(false, error.what(), "");
  }
}

bool Ros2CliManager::isHiddenTopic(const std::string & topic_name)
{
  return ros2_cli::isHiddenTopic(topic_name);
}

bool Ros2CliManager::isHiddenService(const std::string & service_name)
{
  return ros2_cli::isHiddenService(service_name);
}

std::uint8_t Ros2CliManager::effectiveTimeout(std::uint8_t timeout_sec)
{
  return timeout_sec == 0 ? ros2_cli::kDefaultTimeoutSec : timeout_sec;
}

} // namespace ros2_livekit_bridge
