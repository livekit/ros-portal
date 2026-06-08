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

#include <gtest/gtest.h>

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
using Ros2TopicList = Ros2CliManager::Ros2TopicList;

TopicListOptions makeOptions(
  bool show_types = false,
  bool count_topics = false,
  bool include_hidden_topics = false,
  bool verbose = false)
{
  TopicListOptions options;
  options.show_types = show_types;
  options.count_topics = count_topics;
  options.include_hidden_topics = include_hidden_topics;
  options.verbose = verbose;
  return options;
}

class FakeRpcClient : public Ros2CliRpcClient
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
  bool unregistered{false};

  bool hasParticipant(const std::string &) const override
  {
    return has_participant;
  }

  std::string performRpc(
    const std::string & participant_id,
    const std::string & method,
    const std::string & payload,
    std::uint8_t timeout_sec) override
  {
    last_participant_id = participant_id;
    last_method = method;
    last_payload = payload;
    last_timeout_sec = timeout_sec;

    if (rpc_error) {
      throw *rpc_error;
    }
    if (runtime_error) {
      throw *runtime_error;
    }
    return response_json;
  }

  void registerRpcMethod(
    const std::string &,
    RpcHandler handler) override
  {
    registered_handler = std::move(handler);
  }

  void unregisterRpcMethod(const std::string &) override
  {
    unregistered = true;
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
      *node, callback_group, rpc_client);
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

  std::shared_ptr<rclcpp::Node> node;
  rclcpp::CallbackGroup::SharedPtr callback_group;
  std::shared_ptr<FakeRpcClient> rpc_client;
  std::unique_ptr<Ros2CliManager> manager;
};

TEST(Ros2CliManagerFormattingTest, NonVerboseListsTopicNamesOnePerLine)
{
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 0, 0},
    {"/beta", {"std_msgs/msg/Bool"}, 0, 0},
  };

  EXPECT_EQ(
    Ros2CliManager::formatTopicList(topics, makeOptions()),
    "/alpha\n/beta\n");
}

TEST(Ros2CliManagerFormattingTest, ShowTypesListsTopicTypes)
{
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 0, 0},
    {"/beta", {"std_msgs/msg/Bool", "custom_msgs/msg/Thing"}, 0, 0},
  };

  EXPECT_EQ(
    Ros2CliManager::formatTopicList(topics, makeOptions(true)),
    "/alpha [std_msgs/msg/String]\n"
    "/beta [std_msgs/msg/Bool, custom_msgs/msg/Thing]\n");
}

TEST(Ros2CliManagerFormattingTest, CountTopicsOnlyPrintsTopicCount)
{
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 1, 2},
    {"/beta", {"std_msgs/msg/Bool"}, 3, 1},
  };

  EXPECT_EQ(
    Ros2CliManager::formatTopicList(
      topics, makeOptions(true, true, false, true)),
    "2\n");
}

TEST(Ros2CliManagerFormattingTest, VerboseListsPublishedAndSubscribedTopics)
{
  const std::vector<Ros2CliManager::TopicInfo> topics{
    {"/alpha", {"std_msgs/msg/String"}, 1, 2},
    {"/beta", {"std_msgs/msg/String", "custom_msgs/msg/Thing"}, 3, 1},
    {"/quiet", {"std_msgs/msg/Bool"}, 0, 0},
  };

  EXPECT_EQ(
    Ros2CliManager::formatTopicList(
      topics, makeOptions(false, false, false, true)),
    "Published topics:\n"
    " * /alpha [std_msgs/msg/String] 1 publisher\n"
    " * /beta [std_msgs/msg/String, custom_msgs/msg/Thing] 3 publishers\n"
    "\n"
    "Subscribed topics:\n"
    " * /alpha [std_msgs/msg/String] 2 subscribers\n"
    " * /beta [std_msgs/msg/String, custom_msgs/msg/Thing] 1 subscriber\n");
}

TEST(Ros2CliManagerFormattingTest, DetectsHiddenTopicTokens)
{
  EXPECT_FALSE(Ros2CliManager::isHiddenTopic("/visible/topic"));
  EXPECT_TRUE(Ros2CliManager::isHiddenTopic("/_hidden/topic"));
  EXPECT_TRUE(Ros2CliManager::isHiddenTopic("/visible/_hidden"));
}

TEST(Ros2CliManagerFormattingTest, EffectiveTimeoutUsesTenSecondDefault)
{
  EXPECT_EQ(Ros2CliManager::effectiveTimeout(0), 10);
  EXPECT_EQ(Ros2CliManager::effectiveTimeout(7), 7);
}

TEST_F(Ros2CliManagerTest, EmptyParticipantFails)
{
  const auto response = manager->callRemoteTopicList(makeRequest(""));

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

TEST_F(Ros2CliManagerTest, SuccessfulRpcMapsResponseAndDefaultTimeout)
{
  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.err_msg, "");
  EXPECT_EQ(response.output, "/remote_topic\n");
  EXPECT_EQ(rpc_client->last_participant_id, "robot-b");
  EXPECT_EQ(rpc_client->last_method, "ros2_topic_list");
  EXPECT_EQ(rpc_client->last_timeout_sec, 10);

  const auto payload = json::parse(rpc_client->last_payload);
  EXPECT_EQ(payload.at("participant_id"), "robot-b");
  EXPECT_EQ(payload.at("show_types"), false);
  EXPECT_EQ(payload.at("count_topics"), false);
  EXPECT_EQ(payload.at("include_hidden_topics"), false);
  EXPECT_EQ(payload.at("verbose"), false);
  EXPECT_EQ(payload.at("timeout_sec"), 10);
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

TEST_F(Ros2CliManagerTest, RemoteFailureResponsePassesThrough)
{
  rpc_client->response_json =
    R"({"success":false,"err_msg":"remote parse failed","output":""})";

  const auto response = manager->callRemoteTopicList(makeRequest());

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
  EXPECT_EQ(response.err_msg, "unsupported method");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, RuntimeErrorFailsService)
{
  rpc_client->runtime_error = std::runtime_error("send failed");

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "send failed");
  EXPECT_TRUE(response.output.empty());
}

TEST_F(Ros2CliManagerTest, MalformedRpcResponseFailsService)
{
  rpc_client->response_json = "not-json";

  const auto response = manager->callRemoteTopicList(makeRequest());

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.err_msg, "remote ros2_topic_list returned malformed JSON");
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

TEST_F(Ros2CliManagerTest, DestructorUnregistersRpcMethod)
{
  manager.reset();

  EXPECT_TRUE(rpc_client->unregistered);
}

}  // namespace
}  // namespace ros2_livekit_bridge
