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
#include "ros2_livekit_bridge/ros2_cli/types.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge
{
namespace
{

using json = nlohmann::json;
using ros2_cli::Ros2InterfaceShow;
using ros2_cli::Ros2ServiceCall;
using ros2_cli::Ros2ServiceList;
using ros2_cli::Ros2TopicList;
using ros2_cli::Ros2TopicPub;

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

Ros2ServiceCall::Request makeServiceCallRequest(
  std::string interface_type = "std_srvs/srv/SetBool")
{
  Ros2ServiceCall::Request request;
  request.participant_id = "robot-b";
  request.service = "/set_bool";
  request.interface_type = std::move(interface_type);
  request.payload = "{data: true}";
  request.timeout_sec = 0;
  return request;
}

Ros2TopicPub::Request makeTopicPubRequest()
{
  Ros2TopicPub::Request request;
  request.participant_id = "robot-b";
  request.topic = "/cmd_vel";
  request.interface_type = "geometry_msgs/msg/Twist";
  request.payload =
    "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}";
  request.timeout_sec = 0;
  return request;
}

Ros2InterfaceShow::Request makeInterfaceShowRequest()
{
  Ros2InterfaceShow::Request request;
  request.participant_id = "robot-b";
  request.type = "std_msgs/msg/Header";
  request.all_comments = true;
  request.no_comments = false;
  request.timeout_sec = 0;
  return request;
}

TEST(JsonConvertersTest, ConvertsTopicListRequestToOptions) {
  const auto options = topicListOptionsFromRequest(makeRequest());

  EXPECT_TRUE(options.show_types);
  EXPECT_FALSE(options.count_topics);
  EXPECT_TRUE(options.include_hidden_topics);
  EXPECT_TRUE(options.verbose);
}

TEST(JsonConvertersTest, ConvertsTopicPubRequestToOptions) {
  const auto options = topicPubOptionsFromRequest(makeTopicPubRequest());

  EXPECT_EQ(options.topic, "/cmd_vel");
  EXPECT_EQ(options.interface_type, "geometry_msgs/msg/Twist");
  EXPECT_EQ(
    options.payload,
    "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}");
}

TEST(JsonConvertersTest, ConvertsServiceListRequestToOptions) {
  const auto options = serviceListOptionsFromRequest(makeServiceListRequest());

  EXPECT_TRUE(options.show_types);
  EXPECT_FALSE(options.count_services);
  EXPECT_TRUE(options.include_hidden_services);
}

TEST(JsonConvertersTest, ConvertsServiceCallRequestToOptions) {
  const auto options = serviceCallOptionsFromRequest(makeServiceCallRequest());

  EXPECT_EQ(options.service, "/set_bool");
  EXPECT_EQ(options.interface_type, "std_srvs/srv/SetBool");
}

TEST(JsonConvertersTest, ConvertsInterfaceShowRequestToOptions) {
  const auto options =
    interfaceShowOptionsFromRequest(makeInterfaceShowRequest());

  EXPECT_EQ(options.type, "std_msgs/msg/Header");
  EXPECT_TRUE(options.all_comments);
  EXPECT_FALSE(options.no_comments);
}

TEST(JsonConvertersTest, SerializesTopicListRequestPayload) {
  const auto payload = json::parse(topicListRequestToJson(makeRequest(), 7));

  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), true);
  EXPECT_EQ(payload.at("verbose"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(JsonConvertersTest, SerializesTopicPubRequestPayload) {
  const auto payload =
    json::parse(topicPubRequestToJson(makeTopicPubRequest(), 7));

  EXPECT_EQ(payload.at("topic"), "/cmd_vel");
  EXPECT_EQ(payload.at("interface_type"), "geometry_msgs/msg/Twist");
  EXPECT_EQ(
    payload.at("payload"),
    "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}");
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(JsonConvertersTest, SerializesServiceListRequestPayload) {
  const auto payload =
    json::parse(serviceListRequestToJson(makeServiceListRequest(), 7));

  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(JsonConvertersTest, SerializesExplicitServiceCallRequestPayloadAsCdr) {
  std::string error;
  const auto payload = serviceCallRequestToJson(makeServiceCallRequest(), 7, error);
  ASSERT_TRUE(payload.has_value()) << error;
  const auto body = json::parse(*payload);

  EXPECT_EQ(body.at("service"), "/set_bool");
  EXPECT_EQ(body.at("interface_type"), "std_srvs/srv/SetBool");
  EXPECT_EQ(body.at("timeout_sec"), 7);
  ASSERT_TRUE(body.at("request").is_object());
  EXPECT_EQ(body.at("request").at("content_type"), "application/x-ros-cdr");
  EXPECT_FALSE(body.at("request").at("payload_base64").get<std::string>().empty());
  EXPECT_FALSE(body.contains("payload"));
}

TEST(JsonConvertersTest, EmptyServiceCallInterfaceTypeFailsSerialization) {
  std::string error;
  const auto payload = serviceCallRequestToJson(
    makeServiceCallRequest(""), 7, error);

  EXPECT_FALSE(payload.has_value());
  EXPECT_EQ(error, "interface_type must be non-empty");
}

TEST(JsonConvertersTest, SerializesInterfaceShowRequestPayload) {
  const auto payload =
    json::parse(interfaceShowRequestToJson(makeInterfaceShowRequest(), 7));

  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("type"), "std_msgs/msg/Header");
  EXPECT_EQ(payload.at("all_comments"), true);
  EXPECT_EQ(payload.at("no_comments"), false);
  EXPECT_EQ(payload.at("timeout_sec"), 7);
}

TEST(JsonConvertersTest, ParsesTopicListOptionsPayload) {
  std::string error;
  const auto options = topicListOptionsFromJson(
      R"({"show_types":true,"count_topics":true,"include_hidden_topics":true,"verbose":false})",
      error).value();

  EXPECT_TRUE(options.show_types);
  EXPECT_TRUE(options.count_topics);
  EXPECT_TRUE(options.include_hidden_topics);
  EXPECT_FALSE(options.verbose);
}

TEST(JsonConvertersTest, ParsesTopicPubOptionsPayload) {
  std::string error;
  const auto options = topicPubOptionsFromJson(
      R"({"topic":" /cmd_vel ","interface_type":" geometry_msgs/msg/Twist ","payload":"{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}"})",
      error).value();

  EXPECT_EQ(options.topic, "/cmd_vel");
  EXPECT_EQ(options.interface_type, "geometry_msgs/msg/Twist");
  EXPECT_EQ(
    options.payload,
    "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}");
}

TEST(JsonConvertersTest, ParsesServiceListOptionsPayload) {
  std::string error;
  const auto options = serviceListOptionsFromJson(
      R"({"show_types":true,"count_services":true,"include_hidden_services":true})",
      error).value();

  EXPECT_TRUE(options.show_types);
  EXPECT_TRUE(options.count_services);
  EXPECT_TRUE(options.include_hidden_services);
}

TEST(JsonConvertersTest, ParsesServiceCallOptionsPayloadWithCdr) {
  std::string error;
  const auto payload = serviceCallRequestToJson(makeServiceCallRequest(), 7, error);
  ASSERT_TRUE(payload.has_value()) << error;

  const auto options = serviceCallOptionsFromJson(*payload, error).value();

  EXPECT_EQ(options.service, "/set_bool");
  EXPECT_EQ(options.interface_type, "std_srvs/srv/SetBool");
  EXPECT_EQ(options.timeout_sec, 7);
  EXPECT_GT(options.request_payload.size(), 0U);
}

TEST(JsonConvertersTest, EmptyServiceCallInterfaceTypeFailsParsing) {
  std::string error;
  const auto options = serviceCallOptionsFromJson(
      R"({"service":"/set_bool","interface_type":"",)"
      R"("request":{"content_type":"application/x-ros-cdr",)"
      R"("payload_base64":"AQID"}})",
      error);

  EXPECT_FALSE(options.has_value());
  EXPECT_EQ(error, "interface_type must be non-empty");
}

TEST(JsonConvertersTest, ParsesInterfaceShowOptionsPayload) {
  std::string error;
  const auto options = interfaceShowOptionsFromJson(
      R"({"type":"std_msgs/msg/Header","all_comments":true,"no_comments":false})",
      error).value();

  EXPECT_EQ(options.type, "std_msgs/msg/Header");
  EXPECT_TRUE(options.all_comments);
  EXPECT_FALSE(options.no_comments);
}

TEST(JsonConvertersTest, MissingTopicListOptionFieldsDefaultToFalse) {
  std::string error;
  const auto options = topicListOptionsFromJson(R"({})", error).value();

  EXPECT_FALSE(options.show_types);
  EXPECT_FALSE(options.count_topics);
  EXPECT_FALSE(options.include_hidden_topics);
  EXPECT_FALSE(options.verbose);
}

TEST(JsonConvertersTest, MissingServiceListOptionFieldsDefaultToFalse) {
  std::string error;
  const auto options = serviceListOptionsFromJson(R"({})", error).value();

  EXPECT_FALSE(options.show_types);
  EXPECT_FALSE(options.count_services);
  EXPECT_FALSE(options.include_hidden_services);
}

TEST(JsonConvertersTest, MissingInterfaceShowOptionFieldsDefaultToFalse) {
  std::string error;
  const auto options = interfaceShowOptionsFromJson(R"({})", error).value();

  EXPECT_TRUE(options.type.empty());
  EXPECT_FALSE(options.all_comments);
  EXPECT_FALSE(options.no_comments);
}

TEST(JsonConvertersTest, BuildsTopicListResponse) {
  const auto response = makeTopicListResponse(true, "ok", "/topic\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "ok");
  EXPECT_EQ(response.output, "/topic\n");
}

TEST(JsonConvertersTest, BuildsServiceListResponse) {
  const auto response = makeServiceListResponse(true, "ok", "/service\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "ok");
  EXPECT_EQ(response.output, "/service\n");
}

TEST(JsonConvertersTest, BuildsServiceCallResponse) {
  const auto response = makeServiceCallResponse(
    true, "", "success: true\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "success: true\n");
}

TEST(JsonConvertersTest, BuildsInterfaceShowResponse) {
  const auto response = makeInterfaceShowResponse(true, "ok", "string data\n");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "ok");
  EXPECT_EQ(response.output, "string data\n");
}

TEST(JsonConvertersTest, SerializesErrorResponsePayload) {
  const auto payload = json::parse(cliResponseToJson(false, "timeout", ""));

  EXPECT_EQ(payload.at("success"), false);
  EXPECT_EQ(payload.at("err_msg"), "timeout");
  EXPECT_EQ(payload.at("output"), "");
}

TEST(JsonConvertersTest, SerializesSuccessResponsePayload) {
  const auto payload =
    json::parse(cliResponseToJson(true, "", "success: true\n"));

  EXPECT_EQ(payload.at("success"), true);
  EXPECT_EQ(payload.at("err_msg"), "");
  EXPECT_EQ(payload.at("output"), "success: true\n");
}

TEST(JsonConvertersTest, ParsesTopicListResponsePayload) {
  std::string error;
  const auto response = topicListResponseFromJson(
      R"({"success":true,"err_msg":"","output":"/topic\n"})", error).value();

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/topic\n");
}

TEST(JsonConvertersTest, ParsesTopicPubResponsePayload) {
  std::string error;
  const auto response = topicPubResponseFromJson(
      R"({"success":true,"err_msg":"","output":""})", error).value();

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "");
}

TEST(JsonConvertersTest, ParsesServiceListResponsePayload) {
  std::string error;
  const auto response = serviceListResponseFromJson(
      R"({"success":true,"err_msg":"","output":"/service\n"})", error).value();

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/service\n");
}

TEST(JsonConvertersTest, ParsesServiceCallResponsePayload) {
  std::string error;
  const auto response = serviceCallResponseFromJson(
      R"({"success":true,"err_msg":"","output":"success: true\n"})",
      error).value();

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "success: true\n");
}

TEST(JsonConvertersTest, ParsesInterfaceShowResponsePayload) {
  std::string error;
  const auto response = interfaceShowResponseFromJson(
      R"({"success":true,"err_msg":"","output":"string data\n"})",
      error).value();

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "string data\n");
}

TEST(JsonConvertersTest, MalformedPayloadsFail) {
  std::string error;
  EXPECT_FALSE(topicListOptionsFromJson("not-json", error).has_value());
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(topicListResponseFromJson("not-json", error).has_value());
  EXPECT_FALSE(topicListResponseFromJson(R"({"success":true})", error)
    .has_value());
  EXPECT_FALSE(topicPubOptionsFromJson("not-json", error).has_value());
  EXPECT_FALSE(
    topicPubOptionsFromJson(
      R"({"topic":"/cmd","interface_type":"std_msgs/msg/String"})", error)
    .has_value());
  EXPECT_FALSE(
    topicPubOptionsFromJson(
      R"({"topic":"/cmd","interface_type":"std_msgs/msg/String","payload":""})",
      error)
    .has_value());
  EXPECT_FALSE(topicPubResponseFromJson("not-json", error).has_value());
  EXPECT_FALSE(topicPubResponseFromJson(R"({"success":true})", error)
    .has_value());
  EXPECT_FALSE(serviceListOptionsFromJson("not-json", error).has_value());
  EXPECT_FALSE(serviceListResponseFromJson("not-json", error).has_value());
  EXPECT_FALSE(serviceListResponseFromJson(R"({"success":true})", error)
    .has_value());
  EXPECT_FALSE(serviceCallOptionsFromJson("not-json", error).has_value());
  EXPECT_FALSE(
    serviceCallOptionsFromJson(
      R"({"service":"/set_bool","interface_type":"std_srvs/srv/SetBool",)"
      R"("request":{"content_type":"text/plain","payload_base64":"AQID"}})",
      error)
    .has_value());
  EXPECT_FALSE(
    serviceCallOptionsFromJson(
      R"({"service":"/set_bool","interface_type":"std_srvs/srv/SetBool"})",
      error)
    .has_value());
  EXPECT_FALSE(serviceCallResponseFromJson("not-json", error).has_value());
  EXPECT_FALSE(serviceCallResponseFromJson(R"({"success":true})", error)
    .has_value());
  EXPECT_FALSE(interfaceShowOptionsFromJson("not-json", error).has_value());
  EXPECT_FALSE(interfaceShowResponseFromJson("not-json", error).has_value());
  EXPECT_FALSE(interfaceShowResponseFromJson(R"({"success":true})", error)
    .has_value());
}

} // namespace
} // namespace ros2_livekit_bridge
