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
#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge
{
using json = nlohmann::json;
using ros2_cli::Ros2InterfaceShow;
using ros2_cli::Ros2ServiceCall;
using ros2_cli::Ros2ServiceList;
using ros2_cli::Ros2TopicList;
using ros2_cli::Ros2TopicPub;

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

TopicPubOptions topicPubOptionsFromRequest(const Ros2TopicPub::Request & request)
{
  TopicPubOptions options;
  options.topic = request.topic;
  options.interface_type = request.interface_type;
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
  }
         .dump();
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
  }
         .dump();
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
  }
         .dump();
}

std::optional<TopicListOptions> topicListOptionsFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto request = json::parse(payload);

    TopicListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_topics = request.value("count_topics", false);
    options.include_hidden_topics =
      request.value("include_hidden_topics", false);
    options.verbose = request.value("verbose", false);
    return options;
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<TopicPubOptions> topicPubOptionsFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto request = json::parse(payload);
    if (!request.is_object()) {
      error = "Topic pub request must be a JSON object";
      return std::nullopt;
    }

    TopicPubOptions options;
    options.topic = ros2_cli::requiredStringField(
      request, "topic", "topic must be a string", "topic must be non-empty");
    options.interface_type = ros2_cli::requiredStringField(
      request, "interface_type", "interface_type must be a string",
      "interface_type must be non-empty");
    options.payload = ros2_cli::requiredStringField(
      request, "payload", "payload must be a string",
      "payload must be non-empty");
    return options;
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<ServiceListOptions> serviceListOptionsFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto request = json::parse(payload);

    ServiceListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_services = request.value("count_services", false);
    options.include_hidden_services =
      request.value("include_hidden_services", false);
    return options;
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<ServiceCallOptions> serviceCallOptionsFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto request = json::parse(payload);
    if (!request.is_object()) {
      error = "Service call request must be a JSON object";
      return std::nullopt;
    }

    ServiceCallOptions options;
    options.service = ros2_cli::requiredStringField(
      request, "service", "service must be a string",
      "service must be non-empty");
    options.interface_type = ros2_cli::requiredStringField(
      request, "interface_type", "interface_type must be a string",
      "interface_type must be non-empty");
    options.timeout_sec = request.value("timeout_sec", 0);

    const auto request_envelope = request.find("request");
    if (request_envelope == request.end() || !request_envelope->is_object()) {
      error = "request must be a JSON object";
      return std::nullopt;
    }
    const auto content_type = request_envelope->find("content_type");
    if (content_type == request_envelope->end() ||
      !content_type->is_string() ||
      content_type->get<std::string>() != "application/x-ros-cdr")
    {
      error = "request.content_type must be application/x-ros-cdr";
      return std::nullopt;
    }
    const auto payload_base64 = request_envelope->find("payload_base64");
    if (payload_base64 == request_envelope->end() ||
      !payload_base64->is_string())
    {
      error = "request.payload_base64 must be a string";
      return std::nullopt;
    }
    auto bytes = ros2_cli::base64Decode(
      payload_base64->get<std::string>(), error);
    if (!bytes) {
      return std::nullopt;
    }
    if (bytes->empty()) {
      error = "request.payload_base64 must not be empty";
      return std::nullopt;
    }
    options.request_payload = std::move(*bytes);
    return options;
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<InterfaceShowOptions> interfaceShowOptionsFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto request = json::parse(payload);

    InterfaceShowOptions options;
    options.type = request.value("type", "");
    options.all_comments = request.value("all_comments", false);
    options.no_comments = request.value("no_comments", false);
    return options;
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
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

Ros2TopicPub::Response makeTopicPubResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  Ros2TopicPub::Response response;
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

Ros2ServiceCall::Response makeServiceCallResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output)
{
  Ros2ServiceCall::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
}

Ros2InterfaceShow::Response
makeInterfaceShowResponse(
  bool success, const std::string & err_msg,
  const std::string & output)
{
  Ros2InterfaceShow::Response response;
  response.success = success;
  response.err_msg = err_msg;
  response.output = output;
  return response;
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
  }
         .dump();
}

std::optional<Ros2TopicList::Response> topicListResponseFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto parsed = json::parse(payload);
    return makeTopicListResponse(parsed.at("success").get<bool>(),
                                 parsed.at("err_msg").get<std::string>(),
                                 parsed.at("output").get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<Ros2TopicPub::Response> topicPubResponseFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto parsed = json::parse(payload);
    return makeTopicPubResponse(parsed.at("success").get<bool>(),
                                parsed.at("err_msg").get<std::string>(),
                                parsed.at("output").get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<Ros2ServiceList::Response>
serviceListResponseFromJson(const std::string & payload, std::string & error)
{
  try {
    const auto parsed = json::parse(payload);
    return makeServiceListResponse(parsed.at("success").get<bool>(),
                                   parsed.at("err_msg").get<std::string>(),
                                   parsed.at("output").get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<Ros2ServiceCall::Response>
serviceCallResponseFromJson(const std::string & payload, std::string & error)
{
  try {
    const auto parsed = json::parse(payload);
    return makeServiceCallResponse(
      parsed.at("success").get<bool>(),
      parsed.at("err_msg").get<std::string>(),
      parsed.at("output").get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

std::optional<Ros2InterfaceShow::Response>
interfaceShowResponseFromJson(const std::string & payload, std::string & error)
{
  try {
    const auto parsed = json::parse(payload);
    return makeInterfaceShowResponse(parsed.at("success").get<bool>(),
                                     parsed.at("err_msg").get<std::string>(),
                                     parsed.at("output").get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

} // namespace ros2_livekit_bridge
