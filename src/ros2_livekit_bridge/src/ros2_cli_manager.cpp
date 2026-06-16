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

#include <algorithm>
#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <livekit/local_participant.h>
#include <livekit/room.h>
#include <livekit/rpc_error.h>

namespace ros2_livekit_bridge
{
namespace
{

std::string joinTypes(const std::vector<std::string> & types)
{
  std::ostringstream stream;
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << types[i];
  }
  return stream.str();
}

bool hasHiddenNameToken(const std::string & name)
{
  size_t token_start = 0;
  while (token_start < name.size()) {
    while (token_start < name.size() && name[token_start] == '/') {
      ++token_start;
    }
    if (token_start >= name.size()) {
      break;
    }

    const auto token_end = name.find('/', token_start);
    if (name[token_start] == '_') {
      return true;
    }
    if (token_end == std::string::npos) {
      break;
    }
    token_start = token_end + 1;
  }
  return false;
}

}  // namespace

LiveKitRos2CliRpcClient::LiveKitRos2CliRpcClient(livekit::Room & room)
: room_(room)
{
}

bool LiveKitRos2CliRpcClient::hasParticipant(
  const std::string & participant_id) const
{
  return static_cast<bool>(room_.remoteParticipant(participant_id).lock());
}

std::string LiveKitRos2CliRpcClient::performRpc(
  const std::string & participant_id,
  const std::string & method,
  const std::string & payload,
  std::uint8_t timeout_sec)
{
  const auto local_participant = room_.localParticipant().lock();
  if (!local_participant) {
    throw std::runtime_error("LiveKit local participant is unavailable");
  }

  return local_participant->performRpc(
    participant_id, method, payload, static_cast<double>(timeout_sec));
}

void LiveKitRos2CliRpcClient::registerRpcMethod(
  const std::string & method,
  RpcHandler handler)
{
  const auto local_participant = room_.localParticipant().lock();
  if (!local_participant) {
    throw std::runtime_error("LiveKit local participant is unavailable");
  }

  local_participant->registerRpcMethod(
    method,
    [handler = std::move(handler)](
      const livekit::RpcInvocationData & data) -> std::optional<std::string>
    {
      return handler(data.payload);
    });
}

void LiveKitRos2CliRpcClient::unregisterRpcMethod(const std::string & method)
{
  const auto local_participant = room_.localParticipant().lock();
  if (local_participant) {
    local_participant->unregisterRpcMethod(method);
  }
}

Ros2CliManager::Ros2CliManager(
  rclcpp::Node & node,
  rclcpp::CallbackGroup::SharedPtr callback_group,
  std::shared_ptr<Ros2CliRpcClient> rpc_client)
: node_(node), rpc_client_(std::move(rpc_client))
{
  if (!rpc_client_) {
    throw std::invalid_argument("Ros2CliManager requires an RPC client");
  }

  topic_list_service_ = node_.create_service<Ros2TopicList>(
    kTopicListServiceName,
    [this](
      const std::shared_ptr<Ros2TopicList::Request> request,
      std::shared_ptr<Ros2TopicList::Response> response)
    {
      handleTopicListRosService(request, response);
    },
    rclcpp::ServicesQoS(),
    callback_group);

  service_list_service_ = node_.create_service<Ros2ServiceList>(
    kServiceListServiceName,
    [this](
      const std::shared_ptr<Ros2ServiceList::Request> request,
      std::shared_ptr<Ros2ServiceList::Response> response)
    {
      handleServiceListRosService(request, response);
    },
    rclcpp::ServicesQoS(),
    callback_group);

  rpc_client_->registerRpcMethod(
    kTopicListRpcMethod,
    [this](const std::string & payload) {
      return handleTopicListRpc(payload);
    });

  rpc_client_->registerRpcMethod(
    kServiceListRpcMethod,
    [this](const std::string & payload) {
      return handleServiceListRpc(payload);
    });

  RCLCPP_INFO(
    node_.get_logger(),
    "Registered ROS services '%s', '%s' and LiveKit RPC methods '%s', '%s'",
    kTopicListServiceName,
    kServiceListServiceName,
    kTopicListRpcMethod,
    kServiceListRpcMethod);
}

Ros2CliManager::~Ros2CliManager()
{
  if (rpc_client_) {
    rpc_client_->unregisterRpcMethod(kTopicListRpcMethod);
    rpc_client_->unregisterRpcMethod(kServiceListRpcMethod);
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

Ros2CliManager::Ros2TopicList::Response Ros2CliManager::callRemoteTopicList(
  const Ros2TopicList::Request & request) const
{
  if (request.participant_id.empty()) {
    return makeTopicListResponse(false, "participant_id must be non-empty");
  }

  if (!rpc_client_->hasParticipant(request.participant_id)) {
    return makeTopicListResponse(
      false,
      "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = topicListRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = rpc_client_->performRpc(
      request.participant_id, kTopicListRpcMethod, payload, timeout_sec);
  } catch (const livekit::RpcError & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s",
      kTopicListRpcMethod, request.participant_id.c_str(), error.code(),
      error.message().c_str());
    return makeTopicListResponse(false, error.message());
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: %s",
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

  if (!rpc_client_->hasParticipant(request.participant_id)) {
    return makeServiceListResponse(
      false,
      "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = serviceListRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = rpc_client_->performRpc(
      request.participant_id, kServiceListRpcMethod, payload, timeout_sec);
  } catch (const livekit::RpcError & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s",
      kServiceListRpcMethod, request.participant_id.c_str(), error.code(),
      error.message().c_str());
    return makeServiceListResponse(false, error.message());
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: %s",
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

std::string Ros2CliManager::handleTopicListRpc(const std::string & payload) const
{
  try {
    const auto options = topicListOptionsFromJson(payload);
    const auto output = formatTopicList(collectTopicInfo(options), options);
    return topicListResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "Failed to handle LiveKit RPC '%s': %s",
      kTopicListRpcMethod, error.what());
    return topicListResponseToJson(false, error.what(), "");
  }
}

std::string Ros2CliManager::handleServiceListRpc(
  const std::string & payload) const
{
  try {
    const auto options = serviceListOptionsFromJson(payload);
    const auto output = formatServiceList(collectServiceInfo(options), options);
    return serviceListResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "Failed to handle LiveKit RPC '%s': %s",
      kServiceListRpcMethod, error.what());
    return serviceListResponseToJson(false, error.what(), "");
  }
}

bool Ros2CliManager::isHiddenTopic(const std::string & topic_name)
{
  return hasHiddenNameToken(topic_name);
}

bool Ros2CliManager::isHiddenService(const std::string & service_name)
{
  return hasHiddenNameToken(service_name);
}

std::uint8_t Ros2CliManager::effectiveTimeout(std::uint8_t timeout_sec)
{
  return timeout_sec == 0 ? kDefaultTimeoutSec : timeout_sec;
}

std::string Ros2CliManager::formatTopicList(
  const std::vector<TopicInfo> & topics,
  const TopicListOptions & options)
{
  std::ostringstream stream;

  if (options.count_topics) {
    stream << topics.size() << '\n';
    return stream.str();
  }

  if (!options.verbose) {
    for (const auto & topic : topics) {
      stream << topic.name;
      if (options.show_types) {
        stream << " [" << joinTypes(topic.types) << "]";
      }
      stream << '\n';
    }
    return stream.str();
  }

  stream << "Published topics:\n";
  for (const auto & topic : topics) {
    if (topic.publisher_count == 0) {
      continue;
    }
    stream << " * " << topic.name << " [" << joinTypes(topic.types) << "] "
           << topic.publisher_count << " publisher";
    if (topic.publisher_count != 1) {
      stream << 's';
    }
    stream << '\n';
  }

  stream << "\nSubscribed topics:\n";
  for (const auto & topic : topics) {
    if (topic.subscriber_count == 0) {
      continue;
    }
    stream << " * " << topic.name << " [" << joinTypes(topic.types) << "] "
           << topic.subscriber_count << " subscriber";
    if (topic.subscriber_count != 1) {
      stream << 's';
    }
    stream << '\n';
  }

  return stream.str();
}

std::string Ros2CliManager::formatServiceList(
  const std::vector<ServiceInfo> & services,
  const ServiceListOptions & options)
{
  std::ostringstream stream;

  if (options.count_services) {
    stream << services.size() << '\n';
    return stream.str();
  }

  for (const auto & service : services) {
    stream << service.name;
    if (options.show_types) {
      stream << " [" << joinTypes(service.types) << "]";
    }
    stream << '\n';
  }

  return stream.str();
}

std::vector<Ros2CliManager::TopicInfo> Ros2CliManager::collectTopicInfo(
  const TopicListOptions & options) const
{
  std::vector<TopicInfo> topics;
  const auto topic_names_and_types = node_.get_topic_names_and_types();
  topics.reserve(topic_names_and_types.size());

  for (const auto & [topic_name, topic_types] : topic_names_and_types) {
    if (!options.include_hidden_topics && isHiddenTopic(topic_name)) {
      continue;
    }

    TopicInfo topic_info;
    topic_info.name = topic_name;
    topic_info.types = topic_types;
    if (options.verbose) {
      topic_info.publisher_count = node_.count_publishers(topic_name);
      topic_info.subscriber_count = node_.count_subscribers(topic_name);
    }
    topics.push_back(std::move(topic_info));
  }

  std::sort(
    topics.begin(), topics.end(),
    [](const TopicInfo & lhs, const TopicInfo & rhs) {
      return lhs.name < rhs.name;
    });
  return topics;
}

std::vector<Ros2CliManager::ServiceInfo> Ros2CliManager::collectServiceInfo(
  const ServiceListOptions & options) const
{
  std::vector<ServiceInfo> services;
  const auto service_names_and_types = node_.get_service_names_and_types();
  services.reserve(service_names_and_types.size());

  for (const auto & [service_name, service_types] : service_names_and_types) {
    if (!options.include_hidden_services && isHiddenService(service_name)) {
      continue;
    }

    ServiceInfo service_info;
    service_info.name = service_name;
    service_info.types = service_types;
    services.push_back(std::move(service_info));
  }

  std::sort(
    services.begin(), services.end(),
    [](const ServiceInfo & lhs, const ServiceInfo & rhs) {
      return lhs.name < rhs.name;
    });
  return services;
}

}  // namespace ros2_livekit_bridge
