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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge
{
namespace
{

using json = nlohmann::json;

Ros2TopicList::Request makeRequest()
{
  Ros2TopicList::Request request;
  request.participant_id = "robot-b";
  request.show_types = true;
  request.count_topics = false;
  request.include_hidden_topics = true;
  request.verbose = true;
  request.timeout_sec = 0;
  return request;
}

Ros2ServiceList::Request makeServiceListRequest()
{
  Ros2ServiceList::Request request;
  request.participant_id = "robot-b";
  request.show_types = true;
  request.count_services = false;
  request.include_hidden_services = true;
  request.timeout_sec = 0;
  return request;
}

TEST(RosJsonConvertersTest, ConvertsTopicListRequestToOptions)
{
  const auto options = topicListOptionsFromRequest(makeRequest());

  EXPECT_TRUE(options.show_types);
  EXPECT_FALSE(options.count_topics);
  EXPECT_TRUE(options.include_hidden_topics);
  EXPECT_TRUE(options.verbose);
}

TEST(RosJsonConvertersTest, ConvertsServiceListRequestToOptions)
{
  const auto options = serviceListOptionsFromRequest(makeServiceListRequest());

  EXPECT_TRUE(options.show_types);
  EXPECT_FALSE(options.count_services);
  EXPECT_TRUE(options.include_hidden_services);
}

TEST(RosJsonConvertersTest, SerializesTopicListRequestPayload)
{
  const auto payload = json::parse(topicListRequestToJson(makeRequest(), 7));

  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), true);
  EXPECT_EQ(payload.at("verbose"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(RosJsonConvertersTest, SerializesServiceListRequestPayload)
{
  const auto payload = json::parse(
    serviceListRequestToJson(makeServiceListRequest(), 7));

  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(RosJsonConvertersTest, ParsesTopicListOptionsPayload)
{
  const auto options = topicListOptionsFromJson(
    R"({"show_types":true,"count_topics":true,"include_hidden_topics":true,"verbose":false})");

  EXPECT_TRUE(options.show_types);
  EXPECT_TRUE(options.count_topics);
  EXPECT_TRUE(options.include_hidden_topics);
  EXPECT_FALSE(options.verbose);
}

TEST(RosJsonConvertersTest, ParsesServiceListOptionsPayload)
{
  const auto options = serviceListOptionsFromJson(
    R"({"show_types":true,"count_services":true,"include_hidden_services":true})");

  EXPECT_TRUE(options.show_types);
  EXPECT_TRUE(options.count_services);
  EXPECT_TRUE(options.include_hidden_services);
}

TEST(RosJsonConvertersTest, MissingTopicListOptionFieldsDefaultToFalse)
{
  const auto options = topicListOptionsFromJson(R"({})");

  EXPECT_FALSE(options.show_types);
  EXPECT_FALSE(options.count_topics);
  EXPECT_FALSE(options.include_hidden_topics);
  EXPECT_FALSE(options.verbose);
}

TEST(RosJsonConvertersTest, MissingServiceListOptionFieldsDefaultToFalse)
{
  const auto options = serviceListOptionsFromJson(R"({})");

  EXPECT_FALSE(options.show_types);
  EXPECT_FALSE(options.count_services);
  EXPECT_FALSE(options.include_hidden_services);
}

TEST(RosJsonConvertersTest, BuildsTopicListResponse)
{
  const auto response = makeTopicListResponse(true, "ok", "/topic\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "ok");
  EXPECT_EQ(response.output, "/topic\n");
}

TEST(RosJsonConvertersTest, BuildsServiceListResponse)
{
  const auto response = makeServiceListResponse(true, "ok", "/service\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "ok");
  EXPECT_EQ(response.output, "/service\n");
}

TEST(RosJsonConvertersTest, SerializesTopicListResponsePayload)
{
  const auto payload = json::parse(
    topicListResponseToJson(false, "timeout", ""));

  EXPECT_EQ(payload.at("success"), false);
  EXPECT_EQ(payload.at("err_msg"), "timeout");
  EXPECT_EQ(payload.at("output"), "");
}

TEST(RosJsonConvertersTest, SerializesServiceListResponsePayload)
{
  const auto payload = json::parse(
    serviceListResponseToJson(false, "timeout", ""));

  EXPECT_EQ(payload.at("success"), false);
  EXPECT_EQ(payload.at("err_msg"), "timeout");
  EXPECT_EQ(payload.at("output"), "");
}

TEST(RosJsonConvertersTest, ParsesTopicListResponsePayload)
{
  const auto response = topicListResponseFromJson(
    R"({"success":true,"err_msg":"","output":"/topic\n"})");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/topic\n");
}

TEST(RosJsonConvertersTest, ParsesServiceListResponsePayload)
{
  const auto response = serviceListResponseFromJson(
    R"({"success":true,"err_msg":"","output":"/service\n"})");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/service\n");
}

TEST(RosJsonConvertersTest, MalformedPayloadsThrow)
{
  EXPECT_THROW(topicListOptionsFromJson("not-json"), json::exception);
  EXPECT_THROW(topicListResponseFromJson("not-json"), json::exception);
  EXPECT_THROW(
    topicListResponseFromJson(R"({"success":true})"),
    json::exception);
  EXPECT_THROW(serviceListOptionsFromJson("not-json"), json::exception);
  EXPECT_THROW(serviceListResponseFromJson("not-json"), json::exception);
  EXPECT_THROW(
    serviceListResponseFromJson(R"({"success":true})"),
    json::exception);
}

}  // namespace
}  // namespace ros2_livekit_bridge
