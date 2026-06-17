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

#include "ros2_livekit_bridge/ros_json_converters.hpp"

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge
{
using json = nlohmann::json;

TopicListOptions topicListOptionsFromRequest(
  const Ros2TopicList::Request & request)
{
  TopicListOptions options;
  options.show_types = request.show_types;
  options.count_topics = request.count_topics;
  options.include_hidden_topics = request.include_hidden_topics;
  options.verbose = request.verbose;
  return options;
}

ServiceListOptions serviceListOptionsFromRequest(
  const Ros2ServiceList::Request & request)
{
  ServiceListOptions options;
  options.show_types = request.show_types;
  options.count_services = request.count_services;
  options.include_hidden_services = request.include_hidden_services;
  return options;
}

InterfaceShowOptions interfaceShowOptionsFromRequest(
  const Ros2InterfaceShow::Request & request)
{
  InterfaceShowOptions options;
  options.type = request.type;
  options.all_comments = request.all_comments;
  options.no_comments = request.no_comments;
  return options;
}

std::string topicListRequestToJson(
  const Ros2TopicList::Request & request,
  std::uint8_t timeout_sec)
{
  const auto options = topicListOptionsFromRequest(request);
  return json{
    {"participant_id", request.participant_id},
    {"show_types", options.show_types},
    {"count_topics", options.count_topics},
    {"include_hidden_topics", options.include_hidden_topics},
    {"verbose", options.verbose},
    {"timeout_sec", timeout_sec},
  }.dump();
}

std::string serviceListRequestToJson(
  const Ros2ServiceList::Request & request,
  std::uint8_t timeout_sec)
{
  const auto options = serviceListOptionsFromRequest(request);
  return json{
    {"participant_id", request.participant_id},
    {"show_types", options.show_types},
    {"count_services", options.count_services},
    {"include_hidden_services", options.include_hidden_services},
    {"timeout_sec", timeout_sec},
  }.dump();
}

std::string interfaceShowRequestToJson(
  const Ros2InterfaceShow::Request & request,
  std::uint8_t timeout_sec)
{
  const auto options = interfaceShowOptionsFromRequest(request);
  return json{
    {"participant_id", request.participant_id},
    {"type", options.type},
    {"all_comments", options.all_comments},
    {"no_comments", options.no_comments},
    {"timeout_sec", timeout_sec},
  }.dump();
}

TopicListOptions topicListOptionsFromJson(const std::string & payload)
{
  const auto request = json::parse(payload);

  TopicListOptions options;
  options.show_types = request.value("show_types", false);
  options.count_topics = request.value("count_topics", false);
  options.include_hidden_topics = request.value("include_hidden_topics", false);
  options.verbose = request.value("verbose", false);
  return options;
}

ServiceListOptions serviceListOptionsFromJson(const std::string & payload)
{
  const auto request = json::parse(payload);

  ServiceListOptions options;
  options.show_types = request.value("show_types", false);
  options.count_services = request.value("count_services", false);
  options.include_hidden_services =
    request.value("include_hidden_services", false);
  return options;
}

InterfaceShowOptions interfaceShowOptionsFromJson(const std::string & payload)
{
  const auto request = json::parse(payload);

  InterfaceShowOptions options;
  options.type = request.value("type", "");
  options.all_comments = request.value("all_comments", false);
  options.no_comments = request.value("no_comments", false);
  return options;
}

Ros2TopicList::Response makeTopicListResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  Ros2TopicList::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
}

Ros2ServiceList::Response makeServiceListResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  Ros2ServiceList::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
}

Ros2InterfaceShow::Response makeInterfaceShowResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  Ros2InterfaceShow::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
}

std::string topicListResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  return json{
    {"success", success},
    {"err_msg", err_msg},
    {"output", output},
  }.dump();
}

std::string serviceListResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  return json{
    {"success", success},
    {"err_msg", err_msg},
    {"output", output},
  }.dump();
}

std::string interfaceShowResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  return json{
    {"success", success},
    {"err_msg", err_msg},
    {"output", output},
  }.dump();
}

Ros2TopicList::Response topicListResponseFromJson(const std::string & payload)
{
  const auto parsed = json::parse(payload);
  return makeTopicListResponse(
    parsed.at("success").get<bool>(),
    parsed.at("err_msg").get<std::string>(),
    parsed.at("output").get<std::string>());
}

Ros2ServiceList::Response serviceListResponseFromJson(
  const std::string & payload)
{
  const auto parsed = json::parse(payload);
  return makeServiceListResponse(
    parsed.at("success").get<bool>(),
    parsed.at("err_msg").get<std::string>(),
    parsed.at("output").get<std::string>());
}

Ros2InterfaceShow::Response interfaceShowResponseFromJson(
  const std::string & payload)
{
  const auto parsed = json::parse(payload);
  return makeInterfaceShowResponse(
    parsed.at("success").get<bool>(),
    parsed.at("err_msg").get<std::string>(),
    parsed.at("output").get<std::string>());
}

}  // namespace ros2_livekit_bridge
