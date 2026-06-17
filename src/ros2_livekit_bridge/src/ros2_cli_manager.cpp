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
#include <cctype>
#include <exception>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <ament_index_cpp/get_package_share_directory.hpp>
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

std::string leftTrim(const std::string & value)
{
  const auto first = std::find_if(
    value.begin(), value.end(),
    [](unsigned char character) {
      return !std::isspace(character);
    });
  return std::string(first, value.end());
}

std::string rightTrim(std::string value)
{
  while (!value.empty() && std::isspace(
      static_cast<unsigned char>(value.back())))
  {
    value.pop_back();
  }
  return value;
}

std::vector<std::string> splitInterfaceType(const std::string & type)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= type.size()) {
    const auto end = type.find('/', start);
    parts.push_back(type.substr(start, end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

std::string interfacePath(const std::string & type)
{
  const auto parts = splitInterfaceType(type);
  if (parts.size() != 3 || parts[0].empty() || parts[1].empty() ||
    parts[2].empty())
  {
    throw std::runtime_error(
            "Invalid name '" + type +
            "'. Expected three parts separated by '/'");
  }

  const auto & package_name = parts[0];
  const auto & interface_kind = parts[1];
  const auto & interface_name = parts[2];
  if (
    interface_kind != "msg" && interface_kind != "srv" &&
    interface_kind != "action")
  {
    throw std::runtime_error(
            "Invalid interface kind '" + interface_kind +
            "'. Expected 'msg', 'srv', or 'action'");
  }

  const auto share_directory =
    ament_index_cpp::get_package_share_directory(package_name);
  const auto extension = "." + interface_kind;
  return share_directory + "/" + interface_kind + "/" + interface_name +
         extension;
}

std::string removeArraySuffix(std::string type)
{
  const auto array_start = type.find('[');
  if (array_start != std::string::npos) {
    type = type.substr(0, array_start);
  }

  const auto bound_start = type.find('<');
  if (bound_start != std::string::npos) {
    type = type.substr(0, bound_start);
  }
  return type;
}

bool isPrimitiveInterfaceType(const std::string & type)
{
  static const std::set<std::string> kPrimitives{
    "bool",
    "byte",
    "char",
    "float32",
    "float64",
    "int8",
    "uint8",
    "int16",
    "uint16",
    "int32",
    "uint32",
    "int64",
    "uint64",
    "string",
    "wstring",
  };
  return kPrimitives.count(type) > 0;
}

std::string stripTrailingComment(const std::string & line)
{
  const auto comment_start = line.find('#');
  if (comment_start == std::string::npos) {
    return rightTrim(line);
  }
  return rightTrim(line.substr(0, comment_start));
}

std::optional<std::string> nestedInterfaceTypeFromLine(
  const std::string & package_name,
  const std::string & line)
{
  const auto without_comment = stripTrailingComment(line);
  const auto trimmed = leftTrim(without_comment);
  if (trimmed.empty() || trimmed == "---") {
    return std::nullopt;
  }

  std::istringstream stream(trimmed);
  std::string type_token;
  std::string name_token;
  stream >> type_token >> name_token;
  if (type_token.empty() || name_token.empty() ||
    name_token.find('=') != std::string::npos)
  {
    return std::nullopt;
  }

  type_token = removeArraySuffix(type_token);
  if (isPrimitiveInterfaceType(type_token)) {
    return std::nullopt;
  }

  const auto parts = splitInterfaceType(type_token);
  if (parts.size() == 1) {
    if (!type_token.empty() && std::isupper(
        static_cast<unsigned char>(type_token.front())))
    {
      return package_name + "/msg/" + type_token;
    }
    return std::nullopt;
  }
  if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
    return parts[0] + "/msg/" + parts[1];
  }
  if (parts.size() == 3 && parts[1] == "msg") {
    return type_token;
  }
  return std::nullopt;
}

void appendRenderedInterfaceLine(
  std::ostringstream & output,
  const std::string & line,
  bool show_comments,
  int indent_level)
{
  if (show_comments) {
    if (!line.empty()) {
      output << std::string(static_cast<size_t>(indent_level), '\t') << line;
    }
    output << '\n';
    return;
  }

  const auto trimmed = leftTrim(line);
  if (trimmed.empty() || trimmed.front() == '#') {
    return;
  }

  const auto without_comment = stripTrailingComment(line);
  if (without_comment.empty()) {
    return;
  }
  output << std::string(static_cast<size_t>(indent_level), '\t')
         << without_comment << '\n';
}

// `ros2 interface show` expands nested message fields inline, so rendering an
// interface can require walking from a top-level type into child message types.
// Keep this recursive walker in the .cpp anonymous namespace because callers
// only need renderInterfaceDefinition(); the indent level, nested comment mode,
// cycle guard, and output stream are private implementation details.
void renderInterfaceDefinitionRecursive(
  const std::string & type,
  bool show_comments,
  bool show_nested_comments,
  int indent_level,
  std::set<std::string> & active_types,
  std::ostringstream & output)
{
  const auto parts = splitInterfaceType(type);
  const auto path = interfacePath(type);
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("Could not find interface '" + type + "'");
  }

  active_types.insert(type);

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    appendRenderedInterfaceLine(output, line, show_comments, indent_level);

    const auto nested_type = nestedInterfaceTypeFromLine(parts[0], line);
    if (nested_type && active_types.count(*nested_type) == 0) {
      renderInterfaceDefinitionRecursive(
        *nested_type,
        show_nested_comments,
        show_nested_comments,
        indent_level + 1,
        active_types,
        output);
    }
  }

  active_types.erase(type);
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

  interface_show_service_ = node_.create_service<Ros2InterfaceShow>(
    kInterfaceShowServiceName,
    [this](
      const std::shared_ptr<Ros2InterfaceShow::Request> request,
      std::shared_ptr<Ros2InterfaceShow::Response> response)
    {
      handleInterfaceShowRosService(request, response);
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

  rpc_client_->registerRpcMethod(
    kInterfaceShowRpcMethod,
    [this](const std::string & payload) {
      return handleInterfaceShowRpc(payload);
    });

  RCLCPP_INFO(
    node_.get_logger(),
    "Registered ROS services '%s', '%s', '%s' and LiveKit RPC methods '%s', '%s', '%s'",
    kTopicListServiceName,
    kServiceListServiceName,
    kInterfaceShowServiceName,
    kTopicListRpcMethod,
    kServiceListRpcMethod,
    kInterfaceShowRpcMethod);
}

Ros2CliManager::~Ros2CliManager()
{
  if (rpc_client_) {
    rpc_client_->unregisterRpcMethod(kTopicListRpcMethod);
    rpc_client_->unregisterRpcMethod(kServiceListRpcMethod);
    rpc_client_->unregisterRpcMethod(kInterfaceShowRpcMethod);
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

  if (!rpc_client_->hasParticipant(request.participant_id)) {
    return makeInterfaceShowResponse(
      false,
      "LiveKit participant '" + request.participant_id + "' was not found");
  }

  const auto timeout_sec = effectiveTimeout(request.timeout_sec);
  const auto payload = interfaceShowRequestToJson(request, timeout_sec);

  std::string rpc_response;
  try {
    rpc_response = rpc_client_->performRpc(
      request.participant_id, kInterfaceShowRpcMethod, payload, timeout_sec);
  } catch (const livekit::RpcError & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s",
      kInterfaceShowRpcMethod,
      request.participant_id.c_str(),
      error.code(),
      error.message().c_str());
    return makeInterfaceShowResponse(false, error.message());
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: %s",
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

std::string Ros2CliManager::handleInterfaceShowRpc(
  const std::string & payload) const
{
  try {
    const auto options = interfaceShowOptionsFromJson(payload);
    const auto output = renderInterfaceDefinition(options);
    return interfaceShowResponseToJson(true, "", output);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(
      node_.get_logger(),
      "Failed to handle LiveKit RPC '%s': %s",
      kInterfaceShowRpcMethod, error.what());
    return interfaceShowResponseToJson(false, error.what(), "");
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

std::string Ros2CliManager::renderInterfaceDefinition(
  const InterfaceShowOptions & options)
{
  if (options.type.empty()) {
    throw std::runtime_error("the passed value is empty");
  }
  if (options.type == "-") {
    throw std::runtime_error("expected stdin pipe");
  }
  if (options.all_comments && options.no_comments) {
    throw std::runtime_error(
            "all_comments and no_comments are mutually exclusive");
  }

  std::set<std::string> active_types;
  std::ostringstream output;
  renderInterfaceDefinitionRecursive(
    options.type,
    !options.no_comments,
    options.all_comments,
    0,
    active_types,
    output);
  return output.str();
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
