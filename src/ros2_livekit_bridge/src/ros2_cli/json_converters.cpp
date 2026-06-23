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

#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"

#include "ros2_livekit_bridge/ros2_cli/utils.hpp"
#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <exception>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge
{
using json = nlohmann::json;
using ros2_cli::requiredStringField;
using ros2_cli::Ros2InterfaceShow;
using ros2_cli::Ros2ServiceCall;
using ros2_cli::Ros2ServiceList;
using ros2_cli::Ros2TopicList;
using ros2_cli::Ros2TopicPubSrv;

TopicListOptions
topicListOptionsFromRequest(const Ros2TopicList::Request & request)
{
  TopicListOptions options;
  options.show_types = request.show_types;
  options.count_topics = request.count_topics;
  options.include_hidden_topics = request.include_hidden_topics;
  options.verbose = request.verbose;
  return options;
}

TopicPubOptions
topicPubOptionsFromRequest(const Ros2TopicPubSrv::Request & request)
{
  TopicPubOptions options;
  options.topic = request.topic;
  options.msg_type = request.msg_type;
  options.payload = request.payload;
  return options;
}

ServiceListOptions
serviceListOptionsFromRequest(const Ros2ServiceList::Request & request)
{
  ServiceListOptions options;
  options.show_types = request.show_types;
  options.count_services = request.count_services;
  options.include_hidden_services = request.include_hidden_services;
  return options;
}

ServiceCallOptions
serviceCallOptionsFromRequest(const Ros2ServiceCall::Request & request)
{
  ServiceCallOptions options;
  options.service = request.service;
  options.interface_type = request.interface_type;
  options.timeout_sec = request.timeout_sec;
  return options;
}

InterfaceShowOptions
interfaceShowOptionsFromRequest(const Ros2InterfaceShow::Request & request)
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

std::string topicPubRequestToJson(
  const Ros2TopicPubSrv::Request & request,
  std::uint8_t timeout_sec)
{
  const auto options = topicPubOptionsFromRequest(request);
  return json{
    {"topic", options.topic},
    {"msg_type", options.msg_type},
    {"payload", options.payload},
    {"timeout_sec", timeout_sec},
  }.dump();
}

std::string topicPubRequestToJson(
  const Ros2TopicPub::Request & request,
  std::uint8_t timeout_sec)
{
  const auto options = topicPubOptionsFromRequest(request);
  return json{
    {"topic", options.topic},
    {"interface_type", options.interface_type},
    {"payload", options.payload},
    {"timeout_sec", timeout_sec},
  }
         .dump();
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

std::optional<std::string> serviceCallRequestToJson(
  const Ros2ServiceCall::Request & request,
  std::uint8_t timeout_sec,
  std::string & error)
{
  const auto options = serviceCallOptionsFromRequest(request);
  json body{
    {"service", options.service},
    {"interface_type", options.interface_type},
    {"timeout_sec", timeout_sec},
  };

  if (options.interface_type.empty()) {
    error = "interface_type must be non-empty";
    return std::nullopt;
  }

  auto serialized = ros2_cli::serializedMessageFromYaml(
    options.interface_type + "_Request", request.payload, error);
  if (!serialized) {
    return std::nullopt;
  }
  const auto & raw = serialized->get_rcl_serialized_message();
  std::vector<std::uint8_t> bytes;
  if (raw.buffer != nullptr && serialized->size() > 0U) {
    bytes.assign(raw.buffer, raw.buffer + serialized->size());
  }
  body["request"] = json{
    {"content_type", "application/x-ros-cdr"},
    {"payload_base64", ros2_cli::base64Encode(bytes)},
  };

  return body.dump();
}

std::string
interfaceShowRequestToJson(
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

Result<TopicListOptions, std::string>
topicListOptionsFromJson(const std::string & payload)
{
  try {
    const auto request = json::parse(payload);

    TopicListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_topics = request.value("count_topics", false);
    options.include_hidden_topics =
      request.value("include_hidden_topics", false);
    options.verbose = request.value("verbose", false);
    return Result<TopicListOptions, std::string>::ok(std::move(options));
  } catch (const std::exception & parse_error) {
    return Result<TopicListOptions, std::string>::err(parse_error.what());
  }
}

Result<TopicPubOptions, std::string>
topicPubOptionsFromJson(const std::string & payload)
{
  try {
    const auto request = json::parse(payload);
    if (!request.is_object()) {
      return Result<TopicPubOptions, std::string>::err(
          "Topic pub request must be a JSON object");
    }

    TopicPubOptions options;
    options.topic = requiredStringField(
        request, "topic", "topic must be a string", "topic must be non-empty");
    options.msg_type = requiredStringField(
        request, "msg_type", "msg_type must be a string",
        "msg_type must be non-empty");
    options.payload = requiredStringField(
        request, "payload", "payload must be a string",
        "payload must be non-empty");
    return Result<TopicPubOptions, std::string>::ok(std::move(options));
  } catch (const std::exception & parse_error) {
    return Result<TopicPubOptions, std::string>::err(parse_error.what());
  }
}

Result<ServiceListOptions, std::string>
serviceListOptionsFromJson(const std::string & payload)
{
  try {
    const auto request = json::parse(payload);

    ServiceListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_services = request.value("count_services", false);
    options.include_hidden_services =
      request.value("include_hidden_services", false);
    return Result<ServiceListOptions, std::string>::ok(std::move(options));
  } catch (const std::exception & parse_error) {
    return Result<ServiceListOptions, std::string>::err(parse_error.what());
  }
}

Result<InterfaceShowOptions, std::string>
interfaceShowOptionsFromJson(const std::string & payload)
{
  try {
    const auto request = json::parse(payload);

    InterfaceShowOptions options;
    options.type = request.value("type", "");
    options.all_comments = request.value("all_comments", false);
    options.no_comments = request.value("no_comments", false);
    return Result<InterfaceShowOptions, std::string>::ok(std::move(options));
  } catch (const std::exception & parse_error) {
    return Result<InterfaceShowOptions, std::string>::err(parse_error.what());
  }
}

std::string cliResponseToJson(
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

} // namespace ros2_livekit_bridge
