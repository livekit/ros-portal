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

#include "ros2_livekit_bridge/cli/manager.hpp"

#include <gtest/gtest.h>
#include <livekit/rpc_error.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/cli/constants.hpp"
#include "ros2_livekit_bridge/cli/json_converters.hpp"
#include "ros2_livekit_bridge/cli/types.hpp"

namespace ros2_livekit_bridge {
namespace {

using json = nlohmann::json;
using cli::InterfaceShowSrv;
using cli::ServiceCallSrv;
using cli::ServiceListSrv;
using cli::TopicListSrv;
using cli::TopicPubSrv;

// Records the calls the manager makes and produces a cli::Manager::
// LiveKitMethods whose callbacks drive this recorder. This replaces the former
// Ros2CliRpcClient subclass now that the manager takes a struct of callbacks
// instead of a polymorphic interface.
class FakeRpcClient {
public:
  bool has_participant{true};
  std::string response_json{R"({"success":true,"err_msg":"","output":"/remote_topic\n"})"};
  std::optional<livekit::RpcError> rpc_error;
  std::optional<std::runtime_error> runtime_error;
  std::string last_participant_id;
  std::string last_method;
  std::string last_payload;
  std::uint8_t last_timeout_sec{0};
  RpcHandler registered_handler;
  std::vector<std::string> registered_methods;
  std::vector<std::string> unregistered_methods;
  bool register_succeeds{true};
  bool unregister_succeeds{true};

  // Safe to capture `this`: the fixture owns this recorder past the manager's
  // lifetime (the manager is reset before rpc_client in TearDown).
  cli::Manager::LiveKitMethods makeLiveKitMethods() {
    cli::Manager::LiveKitMethods livekit_methods;

    livekit_methods.has_participant = [this](const std::string&) { return has_participant; };

    livekit_methods.perform_rpc = [this](const std::string& participant_id, const std::string& method,
                                         const std::string& payload,
                                         std::uint8_t timeout_sec) -> std::optional<std::string> {
      last_participant_id = participant_id;
      last_method = method;
      last_payload = payload;
      last_timeout_sec = timeout_sec;

      if (rpc_error || runtime_error) {
        return std::nullopt;
      }
      return response_json;
    };

    livekit_methods.register_rpc_method = [this](const std::string& method, RpcHandler handler) {
      if (!register_succeeds) {
        return false;
      }
      registered_methods.push_back(method);
      registered_handler = std::move(handler);
      return true;
    };

    livekit_methods.unregister_rpc_method = [this](const std::string& method) {
      if (!unregister_succeeds) {
        return false;
      }
      unregistered_methods.push_back(method);
      return true;
    };

    return livekit_methods;
  }
};

class ManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    node = std::make_shared<rclcpp::Node>("cli_manager_unit_test");
    callback_group = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    rpc_client = std::make_shared<FakeRpcClient>();
    makeManager();
  }

  void TearDown() override {
    manager.reset();
    rpc_client.reset();
    callback_group.reset();
    node.reset();
  }

  TopicListSrv::Request makeRequest(std::string participant_id = "robot-b", bool verbose = false,
                                    std::uint8_t timeout_sec = 0, bool show_types = false, bool count_topics = false,
                                    bool include_hidden_topics = false) {
    TopicListSrv::Request request;
    request.participant_id = std::move(participant_id);
    request.show_types = show_types;
    request.count_topics = count_topics;
    request.include_hidden_topics = include_hidden_topics;
    request.verbose = verbose;
    request.timeout_sec = timeout_sec;
    return request;
  }

  void makeManager(cli::TopicPublishAllowed topic_publish_allowed = {}) {
    manager = std::make_unique<cli::Manager>(*node, callback_group, rpc_client->makeLiveKitMethods(),
                                             std::move(topic_publish_allowed));
  }

  ServiceListSrv::Request makeServiceRequest(std::string participant_id = "robot-b", std::uint8_t timeout_sec = 0,
                                             bool show_types = false, bool count_services = false,
                                             bool include_hidden_services = false) {
    ServiceListSrv::Request request;
    request.participant_id = std::move(participant_id);
    request.show_types = show_types;
    request.count_services = count_services;
    request.include_hidden_services = include_hidden_services;
    request.timeout_sec = timeout_sec;
    return request;
  }

  ServiceCallSrv::Request makeServiceCallRequest(std::string participant_id = "robot-b",
                                                 std::string service = "/set_bool",
                                                 std::string msg_type = "std_srvs/srv/SetBool",
                                                 std::string payload = "{data: true}", std::uint8_t timeout_sec = 0) {
    ServiceCallSrv::Request request;
    request.participant_id = std::move(participant_id);
    request.service = std::move(service);
    request.msg_type = std::move(msg_type);
    request.payload = std::move(payload);
    request.timeout_sec = timeout_sec;
    return request;
  }

  TopicPubSrv::Request makeTopicPubRequest(std::string participant_id = "robot-b", std::string topic = "/cmd_vel",
                                           std::string msg_type = "std_msgs/msg/String",
                                           std::string payload = "{data: hello}", std::uint8_t timeout_sec = 0) {
    TopicPubSrv::Request request;
    request.participant_id = std::move(participant_id);
    request.topic = std::move(topic);
    request.msg_type = std::move(msg_type);
    request.payload = std::move(payload);
    request.timeout_sec = timeout_sec;
    return request;
  }

  InterfaceShowSrv::Request makeInterfaceRequest(std::string participant_id = "robot-b",
                                                 std::string type = "std_msgs/msg/Header", std::uint8_t timeout_sec = 0,
                                                 bool all_comments = false, bool no_comments = false) {
    InterfaceShowSrv::Request request;
    request.participant_id = std::move(participant_id);
    request.type = std::move(type);
    request.all_comments = all_comments;
    request.no_comments = no_comments;
    request.timeout_sec = timeout_sec;
    return request;
  }

  std::shared_ptr<rclcpp::Node> node;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  std::shared_ptr<FakeRpcClient> rpc_client;
  std::unique_ptr<cli::Manager> manager;
};

TEST(ManagerUtilityTest, EffectiveTimeoutUsesTenSecondDefault) {
  EXPECT_EQ(cli::Manager::effectiveTimeout(0), cli::kDefaultTimeoutSec);
  EXPECT_EQ(cli::Manager::effectiveTimeout(7), 7);
}

TEST(ManagerUtilityTest, ServiceCallRpcTimeoutAddsMargin) {
  EXPECT_EQ(cli::Manager::serviceCallRpcTimeout(1), 2);
  EXPECT_EQ(cli::Manager::serviceCallRpcTimeout(cli::kDefaultTimeoutSec),
            cli::kDefaultTimeoutSec + cli::kServiceCallRpcTimeoutMarginSec);
}

TEST_F(ManagerTest, EmptyParticipantFails) {
  const auto response = manager->callRemoteTopicList(makeRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, EmptyParticipantFailsForServiceList) {
  const auto response = manager->callRemoteServiceList(makeServiceRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, EmptyParticipantFailsForServiceCall) {
  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, EmptyParticipantFailsForTopicPub) {
  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, EmptyParticipantFailsForInterfaceShow) {
  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MissingParticipantFails) {
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteTopicList(makeRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MissingParticipantFailsForServiceList) {
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteServiceList(makeServiceRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MissingParticipantFailsForServiceCall) {
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MissingParticipantFailsForTopicPub) {
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MissingParticipantFailsForInterfaceShow) {
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, SuccessfulRpcMapsResponseAndDefaultTimeout) {
  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/remote_topic\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, cli::kTopicListRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), false);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), false);
  EXPECT_EQ(payload.at("verbose"), false);
  EXPECT_EQ(payload.at("timeout_sec"), cli::kDefaultTimeoutSec);
}

TEST_F(ManagerTest, SuccessfulServiceListRpcMapsResponseAndDefaultTimeout) {
  rpc_client->response_json = R"({"success":true,"err_msg":"","output":"/remote_service\n"})";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/remote_service\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, cli::kServiceListRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), false);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), false);
  EXPECT_EQ(payload.at("timeout_sec"), cli::kDefaultTimeoutSec);
}

TEST_F(ManagerTest, SuccessfulServiceCallRpcMapsResponseAndDefaultTimeout) {
  rpc_client->response_json = R"({"success":true,"err_msg":"",)"
                              R"("output":"success: true\nmessage: enabled\n"})";

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "success: true\nmessage: enabled\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, cli::kServiceCallRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::Manager::serviceCallRpcTimeout(cli::kDefaultTimeoutSec));

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("service"), "/set_bool");
  EXPECT_EQ(payload.at("msg_type"), "std_srvs/srv/SetBool");
  EXPECT_EQ(payload.at("payload"), "{data: true}");
  EXPECT_EQ(payload.at("timeout_sec"), cli::kDefaultTimeoutSec);
  EXPECT_FALSE(payload.contains("request"));
}

TEST_F(ManagerTest, SuccessfulTopicPubRpcMapsResponseAndDefaultTimeout) {
  rpc_client->response_json = R"({"success":true,"err_msg":"","output":""})";

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, cli::kTopicPubRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("topic"), "/cmd_vel");
  EXPECT_EQ(payload.at("msg_type"), "std_msgs/msg/String");
  EXPECT_EQ(payload.at("payload"), "{data: hello}");
  EXPECT_EQ(payload.at("timeout_sec"), cli::kDefaultTimeoutSec);
}

TEST_F(ManagerTest, SuccessfulInterfaceShowRpcMapsResponseAndDefaultTimeout) {
  rpc_client->response_json = R"({"success":true,"err_msg":"","output":"string data\n"})";

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "string data\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, cli::kInterfaceShowRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("type"), "std_msgs/msg/Header");
  EXPECT_EQ(payload.at("all_comments"), false);
  EXPECT_EQ(payload.at("no_comments"), false);
  EXPECT_EQ(payload.at("timeout_sec"), cli::kDefaultTimeoutSec);
}

TEST_F(ManagerTest, PositiveTimeoutPassesThrough) {
  const auto response = manager->callRemoteTopicList(makeRequest("robot-b", true, 3, true, false, true));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), true);
  EXPECT_EQ(payload.at("verbose"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(ManagerTest, PositiveServiceListTimeoutPassesThrough) {
  const auto response = manager->callRemoteServiceList(makeServiceRequest("robot-b", 3, true, false, true));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(ManagerTest, PositiveServiceCallTimeoutPassesThrough) {
  rpc_client->response_json = R"({"success":true,"err_msg":"","output":"success: true\n"})";

  const auto response = manager->callRemoteServiceCall(
      makeServiceCallRequest("robot-b", "/set_bool", "std_srvs/srv/SetBool", "{data: true}", 3));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, cli::Manager::serviceCallRpcTimeout(3));

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("service"), "/set_bool");
  EXPECT_EQ(payload.at("msg_type"), "std_srvs/srv/SetBool");
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(ManagerTest, PositiveTopicPubTimeoutPassesThrough) {
  rpc_client->response_json = R"({"success":true,"err_msg":"","output":""})";

  const auto response = manager->callRemoteTopicPub(
      makeTopicPubRequest("robot-b", "/cmd_vel", "std_msgs/msg/String", "{data: hello}", 3));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("topic"), "/cmd_vel");
  EXPECT_EQ(payload.at("msg_type"), "std_msgs/msg/String");
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(ManagerTest, PositiveInterfaceShowTimeoutPassesThrough) {
  const auto response =
      manager->callRemoteInterfaceShow(makeInterfaceRequest("robot-b", "std_msgs/msg/Header", 3, true, false));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("type"), "std_msgs/msg/Header");
  EXPECT_EQ(payload.at("all_comments"), true);
  EXPECT_EQ(payload.at("no_comments"), false);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(ManagerTest, InterfaceShowMutuallyExclusiveCommentsFail) {
  const auto response =
      manager->callRemoteInterfaceShow(makeInterfaceRequest("robot-b", "std_msgs/msg/Header", 0, true, true));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "all_comments and no_comments are mutually exclusive");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, InvalidTopicPubPayloadFailsBeforeRpc) {
  auto request = makeTopicPubRequest();
  request.payload = "";

  const auto response = manager->callRemoteTopicPub(request);

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("payload must be non-empty"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
  EXPECT_TRUE(rpc_client->last_method.empty());
}

TEST_F(ManagerTest, InvalidServiceCallPayloadFailsBeforeRpc) {
  auto request = makeServiceCallRequest();
  request.payload = "";

  const auto response = manager->callRemoteServiceCall(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "payload must be non-empty");
  EXPECT_TRUE(response.output.empty());
  EXPECT_TRUE(rpc_client->last_method.empty());
}

TEST_F(ManagerTest, EmptyServiceCallInterfaceTypeFailsBeforeRpc) {
  auto request = makeServiceCallRequest();
  request.msg_type = "";

  const auto response = manager->callRemoteServiceCall(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "msg_type must be non-empty");
  EXPECT_TRUE(response.output.empty());
  EXPECT_TRUE(rpc_client->last_method.empty());
}

TEST_F(ManagerTest, RemoteFailureResponsePassesThrough) {
  rpc_client->response_json = R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, ServiceListRemoteFailureResponsePassesThrough) {
  rpc_client->response_json = R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, ServiceCallRemoteFailureResponsePassesThrough) {
  rpc_client->response_json = R"({"success":false,"err_msg":"remote call failed","output":""})";

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote call failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, TopicPubRemoteFailureResponsePassesThrough) {
  rpc_client->response_json = R"({"success":false,"err_msg":"remote publish failed","output":""})";

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote publish failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, InterfaceShowRemoteFailureResponsePassesThrough) {
  rpc_client->response_json = R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RpcErrorFailsService) {
  rpc_client->rpc_error = livekit::RpcError(livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD, "unsupported method");

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RpcErrorFailsServiceList) {
  rpc_client->rpc_error = livekit::RpcError(livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD, "unsupported method");

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RpcErrorFailsServiceCall) {
  rpc_client->rpc_error = livekit::RpcError(livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD, "unsupported method");

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceCallRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RpcErrorFailsTopicPub) {
  rpc_client->rpc_error = livekit::RpcError(livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD, "unsupported method");

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicPubRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RpcErrorFailsInterfaceShow) {
  rpc_client->rpc_error = livekit::RpcError(livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD, "unsupported method");

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kInterfaceShowRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RuntimeErrorFailsService) {
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RuntimeErrorFailsServiceList) {
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RuntimeErrorFailsServiceCall) {
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceCallRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RuntimeErrorFailsTopicPub) {
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicPubRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, RuntimeErrorFailsInterfaceShow) {
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kInterfaceShowRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedRpcResponseFailsService) {
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicListRpcMethod + " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedServiceListRpcResponseFailsService) {
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceListRpcMethod + " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedServiceCallRpcResponseFailsService) {
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteServiceCall(makeServiceCallRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kServiceCallRpcMethod + " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedTopicPubRpcResponseFailsService) {
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteTopicPub(makeTopicPubRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kTopicPubRpcMethod + " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedInterfaceShowRpcResponseFailsService) {
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteInterfaceShow(makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, std::string("remote ") + cli::kInterfaceShowRpcMethod + " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(ManagerTest, MalformedInboundRpcReturnsFailureJson) {
  const auto response_json = manager->handleTopicListRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("parse error"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, MalformedInboundServiceListRpcReturnsFailureJson) {
  const auto response_json = manager->handleServiceListRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("parse error"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, MalformedInboundServiceCallRpcReturnsFailureJson) {
  const auto response_json = manager->handleServiceCallRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("parse error"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, MalformedInboundTopicPubRpcReturnsFailureJson) {
  const auto response_json = manager->handleTopicPubRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("parse error"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, MalformedInboundInterfaceShowRpcReturnsFailureJson) {
  const auto response_json = manager->handleInterfaceShowRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("parse error"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, InvalidInboundInterfaceShowRpcReturnsFailureJson) {
  const auto response_json = manager->handleInterfaceShowRpc(R"({"type":"missing_pkg/msg/Thing"})");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(response.at("err_msg").get<std::string>().find("missing_pkg"), std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(ManagerTest, DestructorUnregistersRpcMethods) {
  manager.reset();

  EXPECT_NE(std::find(rpc_client->unregistered_methods.begin(), rpc_client->unregistered_methods.end(),
                      cli::kTopicListRpcMethod),
            rpc_client->unregistered_methods.end());
  EXPECT_NE(std::find(rpc_client->unregistered_methods.begin(), rpc_client->unregistered_methods.end(),
                      cli::kTopicPubRpcMethod),
            rpc_client->unregistered_methods.end());
  EXPECT_NE(std::find(rpc_client->unregistered_methods.begin(), rpc_client->unregistered_methods.end(),
                      cli::kServiceListRpcMethod),
            rpc_client->unregistered_methods.end());
  EXPECT_NE(std::find(rpc_client->unregistered_methods.begin(), rpc_client->unregistered_methods.end(),
                      cli::kServiceCallRpcMethod),
            rpc_client->unregistered_methods.end());
  EXPECT_NE(std::find(rpc_client->unregistered_methods.begin(), rpc_client->unregistered_methods.end(),
                      cli::kInterfaceShowRpcMethod),
            rpc_client->unregistered_methods.end());
}

} // namespace
} // namespace ros2_livekit_bridge
