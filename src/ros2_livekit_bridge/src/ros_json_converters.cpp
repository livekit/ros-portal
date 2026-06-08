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

Ros2TopicList::Response topicListResponseFromJson(const std::string & payload)
{
  const auto parsed = json::parse(payload);
  return makeTopicListResponse(
    parsed.at("success").get<bool>(),
    parsed.at("err_msg").get<std::string>(),
    parsed.at("output").get<std::string>());
}

}  // namespace ros2_livekit_bridge
