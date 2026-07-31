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

#include "ros_portal/cli/json_converters.hpp"

#include <algorithm>
#include <exception>
#include <nlohmann/json.hpp>

#include "ros_portal/cli/utils.hpp"

namespace ros_portal {
using json = nlohmann::json;
using cli::InterfaceShowSrv;
using cli::requiredStringField;
using cli::ServiceCallSrv;
using cli::ServiceListSrv;
using cli::TopicListSrv;
using cli::TopicPubSrv;

TopicListOptions topicListOptionsFromRequest(const TopicListSrv::Request& request) {
  TopicListOptions options;
  options.show_types = request.show_types;
  options.count_topics = request.count_topics;
  options.include_hidden_topics = request.include_hidden_topics;
  options.verbose = request.verbose;
  return options;
}

TopicPubOptions topicPubOptionsFromRequest(const TopicPubSrv::Request& request) {
  TopicPubOptions options;
  options.topic = request.topic;
  options.msg_type = request.msg_type;
  options.payload = request.payload;
  return options;
}

ServiceListOptions serviceListOptionsFromRequest(const ServiceListSrv::Request& request) {
  ServiceListOptions options;
  options.show_types = request.show_types;
  options.count_services = request.count_services;
  options.include_hidden_services = request.include_hidden_services;
  return options;
}

ServiceCallOptions serviceCallOptionsFromRequest(const ServiceCallSrv::Request& request) {
  ServiceCallOptions options;
  options.service = request.service;
  options.msg_type = request.msg_type;
  options.payload = request.payload;
  options.timeout_sec = request.timeout_sec;
  return options;
}

InterfaceShowOptions interfaceShowOptionsFromRequest(const InterfaceShowSrv::Request& request) {
  InterfaceShowOptions options;
  options.type = request.type;
  options.all_comments = request.all_comments;
  options.no_comments = request.no_comments;
  return options;
}

std::string topicListRequestToJson(const TopicListSrv::Request& request, std::uint8_t timeout_sec) {
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

std::string topicPubRequestToJson(const TopicPubSrv::Request& request, std::uint8_t timeout_sec) {
  const auto options = topicPubOptionsFromRequest(request);
  return json{
      {"topic", options.topic},
      {"msg_type", options.msg_type},
      {"payload", options.payload},
      {"timeout_sec", timeout_sec},
  }
      .dump();
}

std::string serviceListRequestToJson(const ServiceListSrv::Request& request, std::uint8_t timeout_sec) {
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

std::string serviceCallRequestToJson(const ServiceCallSrv::Request& request, std::uint8_t timeout_sec) {
  const auto options = serviceCallOptionsFromRequest(request);
  return json{
      {"service", options.service},
      {"msg_type", options.msg_type},
      {"payload", options.payload},
      {"timeout_sec", timeout_sec},
  }
      .dump();
}

std::string interfaceShowRequestToJson(const InterfaceShowSrv::Request& request, std::uint8_t timeout_sec) {
  const auto options = interfaceShowOptionsFromRequest(request);
  return json{
      {"participant_id", request.participant_id}, {"type", options.type},       {"all_comments", options.all_comments},
      {"no_comments", options.no_comments},       {"timeout_sec", timeout_sec},
  }
      .dump();
}

livekit::Result<TopicListOptions, std::string> topicListOptionsFromJson(const std::string& payload) {
  try {
    const auto request = json::parse(payload);

    TopicListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_topics = request.value("count_topics", false);
    options.include_hidden_topics = request.value("include_hidden_topics", false);
    options.verbose = request.value("verbose", false);
    return livekit::Result<TopicListOptions, std::string>::success(std::move(options));
  } catch (const std::exception& parse_error) {
    return livekit::Result<TopicListOptions, std::string>::failure(parse_error.what());
  }
}

livekit::Result<TopicPubOptions, std::string> topicPubOptionsFromJson(const std::string& payload) {
  try {
    const auto request = json::parse(payload);
    if (!request.is_object()) {
      return livekit::Result<TopicPubOptions, std::string>::failure("Topic pub request must be a JSON object");
    }

    TopicPubOptions options;
    options.topic = requiredStringField(request, "topic", "topic must be a string", "topic must be non-empty");
    options.msg_type =
        requiredStringField(request, "msg_type", "msg_type must be a string", "msg_type must be non-empty");
    options.payload = requiredStringField(request, "payload", "payload must be a string", "payload must be non-empty");
    return livekit::Result<TopicPubOptions, std::string>::success(std::move(options));
  } catch (const std::exception& parse_error) {
    return livekit::Result<TopicPubOptions, std::string>::failure(parse_error.what());
  }
}

livekit::Result<ServiceListOptions, std::string> serviceListOptionsFromJson(const std::string& payload) {
  try {
    const auto request = json::parse(payload);

    ServiceListOptions options;
    options.show_types = request.value("show_types", false);
    options.count_services = request.value("count_services", false);
    options.include_hidden_services = request.value("include_hidden_services", false);
    return livekit::Result<ServiceListOptions, std::string>::success(std::move(options));
  } catch (const std::exception& parse_error) {
    return livekit::Result<ServiceListOptions, std::string>::failure(parse_error.what());
  }
}

std::optional<ServiceCallOptions> serviceCallOptionsFromJson(const std::string& payload, std::string& error) {
  try {
    const auto request = json::parse(payload);
    if (!request.is_object()) {
      error = "Service call request must be a JSON object";
      return std::nullopt;
    }

    ServiceCallOptions options;
    options.service =
        cli::requiredStringField(request, "service", "service must be a string", "service must be non-empty");
    options.msg_type =
        cli::requiredStringField(request, "msg_type", "msg_type must be a string", "msg_type must be non-empty");
    options.payload =
        cli::requiredStringField(request, "payload", "payload must be a string", "payload must be non-empty");
    // Clamp to uint8_t range: negative -> 0, values above 255 -> 255.
    const int raw_timeout_sec = request.value("timeout_sec", 0);
    options.timeout_sec = static_cast<std::uint8_t>(std::clamp(raw_timeout_sec, 0, 255));
    error.clear(); // success, clear any previous error
    return options;
  } catch (const std::exception& parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

livekit::Result<InterfaceShowOptions, std::string> interfaceShowOptionsFromJson(const std::string& payload) {
  try {
    const auto request = json::parse(payload);

    InterfaceShowOptions options;
    options.type = request.value("type", "");
    options.all_comments = request.value("all_comments", false);
    options.no_comments = request.value("no_comments", false);
    return livekit::Result<InterfaceShowOptions, std::string>::success(std::move(options));
  } catch (const std::exception& parse_error) {
    return livekit::Result<InterfaceShowOptions, std::string>::failure(parse_error.what());
  }
}

std::string cliResponseToJson(bool success, const std::string& err_msg, const std::string& output) {
  return json{
      {"success", success},
      {"err_msg", err_msg},
      {"output", output},
  }
      .dump();
}

} // namespace ros_portal
