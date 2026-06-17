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
#include "ros2_livekit_bridge/ros2_cli/ros2_interface_show.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_service_list.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_topic_list.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros_json_converters.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace ros2_livekit_bridge
{
Ros2CliManager::Ros2CliManager(
  rclcpp::Node & node,
  rclcpp::CallbackGroup::SharedPtr callback_group,
  RpcTransport transport)
: node_(node), transport_(std::move(transport))
{
  if (!transport_.has_participant || !transport_.perform_rpc ||
      !transport_.register_rpc_method || !transport_.unregister_rpc_method)
  {
    throw std::invalid_argument(
            "Ros2CliManager requires a fully populated RpcTransport");
  }

  topic_list_service_ = node_.create_service<Ros2TopicList>(
      kTopicListServiceName,
    [this](const std::shared_ptr<Ros2TopicList::Request> request,
    std::shared_ptr<Ros2TopicList::Response> response) {
      handleTopicListRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  service_list_service_ = node_.create_service<Ros2ServiceList>(
      kServiceListServiceName,
    [this](const std::shared_ptr<Ros2ServiceList::Request> request,
    std::shared_ptr<Ros2ServiceList::Response> response) {
      handleServiceListRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  interface_show_service_ = node_.create_service<Ros2InterfaceShow>(
      kInterfaceShowServiceName,
    [this](const std::shared_ptr<Ros2InterfaceShow::Request> request,
    std::shared_ptr<Ros2InterfaceShow::Response> response) {
      handleInterfaceShowRosService(request, response);
      },
      rclcpp::ServicesQoS(), callback_group);

  transport_.register_rpc_method(kTopicListRpcMethod,
    [this](const std::string & payload) {
      return handleTopicListRpc(payload);
                                 });

  transport_.register_rpc_method(kServiceListRpcMethod,
    [this](const std::string & payload) {
      return handleServiceListRpc(payload);
                                 });

  transport_.register_rpc_method(kInterfaceShowRpcMethod,
    [this](const std::string & payload) {
      return handleInterfaceShowRpc(payload);
                                 });

  RCLCPP_INFO(node_.get_logger(),
              "Registered ROS services '%s', '%s', '%s' and LiveKit RPC "
              "methods '%s', '%s', '%s'",
              kTopicListServiceName, kServiceListServiceName,
              kInterfaceShowServiceName, kTopicListRpcMethod,
              kServiceListRpcMethod, kInterfaceShowRpcMethod);
}

Ros2CliManager::~Ros2CliManager()
{
  if (transport_.unregister_rpc_method) {
    transport_.unregister_rpc_method(kTopicListRpcMethod);
    transport_.unregister_rpc_method(kServiceListRpcMethod);
    transport_.unregister_rpc_method(kInterfaceShowRpcMethod);
  }
}

void Ros2CliManager::handleTopicListRosService(
  const std::shared_ptr<Ros2TopicList::Request> request,
  std::shared_ptr<Ros2TopicList::Response> response) const
{
  *response = callRemoteTopicList(*request);
}

void Ros2CliManager::handleServiceListRosService(
  const std::shared_ptr<Ros2ServiceList::Request> request,
  std::shared_ptr<Ros2ServiceList::Response> response) const
{
  *response = callRemoteServiceList(*request);
}

void Ros2CliManager::handleInterfaceShowRosService(
  const std::shared_ptr<Ros2InterfaceShow::Request> request,
  std::shared_ptr<Ros2InterfaceShow::Response> response) const
{
  *response = callRemoteInterfaceShow(*request);
}

Ros2CliManager::Ros2TopicList::Response Ros2CliManager::callRemoteTopicList(
  const Ros2TopicList::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeTopicListResponse(false, "participant_id must be non-empty");
  }

  if (!transport_.has_participant(request.participant_id)) {
    return makeTopicListResponse(false, "LiveKit participant '" +
                                            request.participant_id +
                                            "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicListRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = transport_.perform_rpc(
        request.participant_id, kTopicListRpcMethod, payload, timeout_sec);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(), "LiveKit RPC '%s' to participant '%s' failed: %s",
        kTopicListRpcMethod, request.participant_id.c_str(), error.what());
    return makeTopicListResponse(false, error.what());
  }

  try {
    return topicListResponseFromJson(rpc_response);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(),
        "LiveKit RPC '%s' from participant '%s' returned malformed JSON: %s",
        kTopicListRpcMethod, request.participant_id.c_str(), error.what());
    return makeTopicListResponse(
        false, "remote ros2_topic_list returned malformed JSON");
  }
}

Ros2CliManager::Ros2ServiceList::Response Ros2CliManager::callRemoteServiceList(
  const Ros2ServiceList::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeServiceListResponse(false, "participant_id must be non-empty");
  }

  if (!transport_.has_participant(request.participant_id)) {
    return makeServiceListResponse(false, "LiveKit participant '" +
                                              request.participant_id +
                                              "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = serviceListRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = transport_.perform_rpc(
        request.participant_id, kServiceListRpcMethod, payload, timeout_sec);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(), "LiveKit RPC '%s' to participant '%s' failed: %s",
        kServiceListRpcMethod, request.participant_id.c_str(), error.what());
    return makeServiceListResponse(false, error.what());
  }

  try {
    return serviceListResponseFromJson(rpc_response);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(),
        "LiveKit RPC '%s' from participant '%s' returned malformed JSON: %s",
        kServiceListRpcMethod, request.participant_id.c_str(), error.what());
    return makeServiceListResponse(
        false, "remote ros2_service_list returned malformed JSON");
  }
}

Ros2CliManager::Ros2InterfaceShow::Response
Ros2CliManager::callRemoteInterfaceShow(
  const Ros2InterfaceShow::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeInterfaceShowResponse(false, "participant_id must be non-empty");
  }

  if (request.all_comments && request.no_comments) {
    return makeInterfaceShowResponse(
        false, "all_comments and no_comments are mutually exclusive");
  }

  if (!transport_.has_participant(request.participant_id)) {
    return makeInterfaceShowResponse(false, "LiveKit participant '" +
                                                request.participant_id +
                                                "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = interfaceShowRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = transport_.perform_rpc(
        request.participant_id, kInterfaceShowRpcMethod, payload, timeout_sec);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(), "LiveKit RPC '%s' to participant '%s' failed: %s",
        kInterfaceShowRpcMethod, request.participant_id.c_str(), error.what());
    return makeInterfaceShowResponse(false, error.what());
  }

  try {
    return interfaceShowResponseFromJson(rpc_response);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
        node_.get_logger(),
        "LiveKit RPC '%s' from participant '%s' returned malformed JSON: %s",
        kInterfaceShowRpcMethod, request.participant_id.c_str(), error.what());
    return makeInterfaceShowResponse(
        false, "remote ros2_interface_show returned malformed JSON");
  }
}

std::string
Ros2CliManager::handleTopicListRpc(const std::string & payload) const
{
  try {
    const auto options = topicListOptionsFromJson(payload);
    const auto output = ros2_cli::formatTopicList(
        ros2_cli::collectTopicInfo(node_, options), options);
    return topicListResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_.get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kTopicListRpcMethod, error.what());
    return topicListResponseToJson(false, error.what(), "");
  }
}

std::string
Ros2CliManager::handleServiceListRpc(const std::string & payload) const
{
  try {
    const auto options = serviceListOptionsFromJson(payload);
    const auto output = ros2_cli::formatServiceList(
        ros2_cli::collectServiceInfo(node_, options), options);
    return serviceListResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_.get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kServiceListRpcMethod, error.what());
    return serviceListResponseToJson(false, error.what(), "");
  }
}

std::string
Ros2CliManager::handleInterfaceShowRpc(const std::string & payload) const
{
  try {
    const auto options = interfaceShowOptionsFromJson(payload);
    const auto output = ros2_cli::renderInterfaceDefinition(options);
    return interfaceShowResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node_.get_logger(), "Failed to handle LiveKit RPC '%s': %s",
                 kInterfaceShowRpcMethod, error.what());
    return interfaceShowResponseToJson(false, error.what(), "");
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
  return timeout_sec == 0 ? kDefaultTimeoutSec : timeout_sec;
}

} // namespace ros2_livekit_bridge
