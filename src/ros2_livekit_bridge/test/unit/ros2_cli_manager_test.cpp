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

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/types.hpp"
#include "ros2_livekit_bridge/ros2_cli_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <livekit/rpc_error.h>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

namespace ros2_livekit_bridge
{
namespace
{

using json = nlohmann::json;
using ros2_cli::Ros2InterfaceShow;
using ros2_cli::Ros2ServiceList;
using ros2_cli::Ros2TopicList;

// Records the calls the manager makes and produces a Ros2CliManager::
// LivekitMethods whose callbacks drive this recorder. This replaces the former
// Ros2CliRpcClient subclass now that the manager takes a struct of callbacks
// instead of a polymorphic interface.
class FakeRpcClient
{
public:
  bool has_participant{true};
  std::string response_json{
    R"({"success":true,"err_msg":"","output":"/remote_topic\n"})"};
  std::optional<livekit::RpcError> rpc_error;
  std::optional<std::runtime_error> runtime_error;
  std::string last_participant_id;
  std::string last_method;
  std::string last_payload;
  std::uint8_t last_timeout_sec{0};
  RpcHandler registered_handler;
  std::vector<std::string> registered_methods;
  std::vector<std::string> unregistered_methods;

  // Safe to capture `this`: the fixture owns this recorder past the manager's
  // lifetime (the manager is reset before rpc_client in TearDown).
  Ros2CliManager::LivekitMethods makeLivekitMethods()
  {
    Ros2CliManager::LivekitMethods livekit_methods;

    livekit_methods.has_participant =
      [this](const std::string &) { return has_participant; };

    livekit_methods.perform_rpc =
      [this](const std::string & participant_id, const std::string & method,
        const std::string & payload, std::uint8_t timeout_sec)
        -> std::optional<std::string> {
        last_participant_id = participant_id;
        last_method = method;
        last_payload = payload;
        last_timeout_sec = timeout_sec;

        if (rpc_error || runtime_error) {
          return std::nullopt;
        }
        return response_json;
      };

    livekit_methods.register_rpc_method =
      [this](const std::string & method, RpcHandler handler) {
        registered_methods.push_back(method);
        registered_handler = std::move(handler);
      };

    livekit_methods.unregister_rpc_method =
      [this](const std::string & method) {
        unregistered_methods.push_back(method);
      };

    return livekit_methods;
  }
};

class Ros2CliManagerTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    int argc = 0;
    char ** argv = nullptr;
    rclcpp::init(argc, argv);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }

  void SetUp() override
  {
    node = std::make_shared<rclcpp::Node>("ros2_cli_manager_unit_test");
    callback_group =
      node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    rpc_client = std::make_shared<FakeRpcClient>();
    manager = std::make_unique<Ros2CliManager>(
      *node, callback_group, rpc_client->makeLivekitMethods());
  }

  void TearDown() override
  {
    manager.reset();
    rpc_client.reset();
    callback_group.reset();
    node.reset();
  }

  Ros2TopicList::Request makeRequest(
    std::string participant_id = "robot-b",
    bool verbose = false,
    std::uint8_t timeout_sec = 0,
    bool show_types = false,
    bool count_topics = false,
    bool include_hidden_topics = false)
  {
    Ros2TopicList::Request request;
    request.participant_id = std::move(participant_id);
    request.show_types = show_types;
    request.count_topics = count_topics;
    request.include_hidden_topics = include_hidden_topics;
    request.verbose = verbose;
    request.timeout_sec = timeout_sec;
    return request;
  }

  Ros2ServiceList::Request makeServiceRequest(
    std::string participant_id = "robot-b",
    std::uint8_t timeout_sec = 0,
    bool show_types = false,
    bool count_services = false,
    bool include_hidden_services = false)
  {
    Ros2ServiceList::Request request;
    request.participant_id = std::move(participant_id);
    request.show_types = show_types;
    request.count_services = count_services;
    request.include_hidden_services = include_hidden_services;
    request.timeout_sec = timeout_sec;
    return request;
  }

  Ros2InterfaceShow::Request makeInterfaceRequest(
    std::string participant_id = "robot-b",
    std::string type = "std_msgs/msg/Header",
    std::uint8_t timeout_sec = 0,
    bool all_comments = false,
    bool no_comments = false)
  {
    Ros2InterfaceShow::Request request;
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
  std::unique_ptr<Ros2CliManager> manager;
};

TEST(Ros2CliManagerUtilityTest, EffectiveTimeoutUsesTenSecondDefault)
{
  EXPECT_EQ(Ros2CliManager::effectiveTimeout(0), ros2_cli::kDefaultTimeoutSec);
  EXPECT_EQ(Ros2CliManager::effectiveTimeout(7), 7);
}

TEST_F(Ros2CliManagerTest, EmptyParticipantFails)
{
  const auto response = manager->callRemoteTopicList(makeRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, EmptyParticipantFailsForServiceList)
{
  const auto response = manager->callRemoteServiceList(makeServiceRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, EmptyParticipantFailsForInterfaceShow)
{
  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest(""));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "participant_id must be non-empty");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MissingParticipantFails)
{
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteTopicList(makeRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MissingParticipantFailsForServiceList)
{
  rpc_client->has_participant = false;

  const auto response =
    manager->callRemoteServiceList(makeServiceRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MissingParticipantFailsForInterfaceShow)
{
  rpc_client->has_participant = false;

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest("missing"));

  EXPECT_FALSE(response.success);
  EXPECT_NE(response.err_msg.find("missing"), std::string::npos);
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, SuccessfulRpcMapsResponseAndDefaultTimeout)
{
  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/remote_topic\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, ros2_cli::kTopicListRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, ros2_cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), false);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), false);
  EXPECT_EQ(payload.at("verbose"), false);
  EXPECT_EQ(payload.at("timeout_sec"), ros2_cli::kDefaultTimeoutSec);
}

TEST_F(Ros2CliManagerTest, SuccessfulServiceListRpcMapsResponseAndDefaultTimeout)
{
  rpc_client->response_json =
    R"({"success":true,"err_msg":"","output":"/remote_service\n"})";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/remote_service\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, ros2_cli::kServiceListRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, ros2_cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), false);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), false);
  EXPECT_EQ(payload.at("timeout_sec"), ros2_cli::kDefaultTimeoutSec);
}

TEST_F(Ros2CliManagerTest, SuccessfulInterfaceShowRpcMapsResponseAndDefaultTimeout)
{
  rpc_client->response_json =
    R"({"success":true,"err_msg":"","output":"string data\n"})";

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "string data\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, ros2_cli::kInterfaceShowRpcMethod);
  EXPECT_EQ(rpc_client->last_timeout_sec, ros2_cli::kDefaultTimeoutSec);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("type"), "std_msgs/msg/Header");
  EXPECT_EQ(payload.at("all_comments"), false);
  EXPECT_EQ(payload.at("no_comments"), false);
  EXPECT_EQ(payload.at("timeout_sec"), ros2_cli::kDefaultTimeoutSec);
}

TEST_F(Ros2CliManagerTest, PositiveTimeoutPassesThrough)
{
  const auto response = manager->callRemoteTopicList(
    makeRequest("robot-b", true, 3, true, false, true));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), true);
  EXPECT_EQ(payload.at("verbose"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(Ros2CliManagerTest, PositiveServiceListTimeoutPassesThrough)
{
  const auto response = manager->callRemoteServiceList(
    makeServiceRequest("robot-b", 3, true, false, true));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("show_types"), true);
  EXPECT_EQ(payload.at("count_services"), false);
  EXPECT_EQ(payload.at("include_hidden_services"), true);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(Ros2CliManagerTest, PositiveInterfaceShowTimeoutPassesThrough)
{
  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest(
      "robot-b", "std_msgs/msg/Header", 3, true, false));

  EXPECT_TRUE(response.success);
  EXPECT_EQ(rpc_client->last_timeout_sec, 3);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("type"), "std_msgs/msg/Header");
  EXPECT_EQ(payload.at("all_comments"), true);
  EXPECT_EQ(payload.at("no_comments"), false);
  EXPECT_EQ(payload.at("timeout_sec"), 3);
}

TEST_F(Ros2CliManagerTest, InterfaceShowMutuallyExclusiveCommentsFail)
{
  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest(
      "robot-b", "std_msgs/msg/Header", 0, true, true));

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg, "all_comments and no_comments are mutually exclusive");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RemoteFailureResponsePassesThrough)
{
  rpc_client->response_json =
    R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, ServiceListRemoteFailureResponsePassesThrough)
{
  rpc_client->response_json =
    R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, InterfaceShowRemoteFailureResponsePassesThrough)
{
  rpc_client->response_json =
    R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote parse failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RpcErrorFailsService)
{
  rpc_client->rpc_error = livekit::RpcError(
    livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD,
    "unsupported method");

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kTopicListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RpcErrorFailsServiceList)
{
  rpc_client->rpc_error = livekit::RpcError(
    livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD,
    "unsupported method");

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kServiceListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RpcErrorFailsInterfaceShow)
{
  rpc_client->rpc_error = livekit::RpcError(
    livekit::RpcError::ErrorCode::UNSUPPORTED_METHOD,
    "unsupported method");

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kInterfaceShowRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RuntimeErrorFailsService)
{
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kTopicListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RuntimeErrorFailsServiceList)
{
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kServiceListRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RuntimeErrorFailsInterfaceShow)
{
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kInterfaceShowRpcMethod + " RPC failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MalformedRpcResponseFailsService)
{
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kTopicListRpcMethod +
    " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MalformedServiceListRpcResponseFailsService)
{
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteServiceList(makeServiceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kServiceListRpcMethod +
    " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MalformedInterfaceShowRpcResponseFailsService)
{
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteInterfaceShow(
    makeInterfaceRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(
    response.err_msg,
    std::string("remote ") + ros2_cli::kInterfaceShowRpcMethod +
    " returned malformed JSON");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MalformedInboundRpcReturnsFailureJson)
{
  const auto response_json = manager->handleTopicListRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(
    response.at("err_msg").get<std::string>().find("parse error"),
    std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(Ros2CliManagerTest, MalformedInboundServiceListRpcReturnsFailureJson)
{
  const auto response_json = manager->handleServiceListRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(
    response.at("err_msg").get<std::string>().find("parse error"),
    std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(Ros2CliManagerTest, MalformedInboundInterfaceShowRpcReturnsFailureJson)
{
  const auto response_json = manager->handleInterfaceShowRpc("not-json");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(
    response.at("err_msg").get<std::string>().find("parse error"),
    std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(Ros2CliManagerTest, InvalidInboundInterfaceShowRpcReturnsFailureJson)
{
  const auto response_json = manager->handleInterfaceShowRpc(
    R"({"type":"missing_pkg/msg/Thing"})");
  const auto response = json::parse(response_json);

  EXPECT_EQ(response.at("success"), false);
  EXPECT_NE(
    response.at("err_msg").get<std::string>().find("missing_pkg"),
    std::string::npos);
  EXPECT_EQ(response.at("output"), "");
}

TEST_F(Ros2CliManagerTest, DestructorUnregistersRpcMethods)
{
  manager.reset();

  EXPECT_NE(
    std::find(
      rpc_client->unregistered_methods.begin(),
      rpc_client->unregistered_methods.end(),
      ros2_cli::kTopicListRpcMethod),
    rpc_client->unregistered_methods.end());
  EXPECT_NE(
    std::find(
      rpc_client->unregistered_methods.begin(),
      rpc_client->unregistered_methods.end(),
      ros2_cli::kServiceListRpcMethod),
    rpc_client->unregistered_methods.end());
  EXPECT_NE(
    std::find(
      rpc_client->unregistered_methods.begin(),
      rpc_client->unregistered_methods.end(),
      ros2_cli::kInterfaceShowRpcMethod),
    rpc_client->unregistered_methods.end());
}

}  // namespace
}  // namespace ros2_livekit_bridge
