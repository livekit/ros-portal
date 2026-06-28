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

#include "../bridge_e2e_fixture.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace ros2_livekit_bridge::test
{
namespace
{

TEST_F(BridgeTestE2E, ListsRemoteRosTopicsOverRpc) {
  initializeRuntime(kBidirectionalTopic);

  ASSERT_TRUE(
      waitFor([&]() {return publisherB()->get_subscription_count() > 0;},
              kGraphTimeout))
      << "Bridge B did not subscribe to " << kBidirectionalTopic;

  constexpr const char *kHiddenTopic = "/_hidden_topic";
  auto hidden_publisher =
    robotBNode()->create_publisher<std_msgs::msg::String>(kHiddenTopic, 10);
  ASSERT_TRUE(
      waitFor([&]() {return topicExists(*robotBNode(), kHiddenTopic);},
              kGraphTimeout))
      << "Hidden topic did not appear in bridge B graph";

  const auto response = callTopicListService(robotANode(), identityB());
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success) << response->err_msg;
  EXPECT_TRUE(contains(response->output, kBidirectionalTopic));
  EXPECT_FALSE(contains(response->output, "[std_msgs/msg/String]"));
  EXPECT_FALSE(contains(response->output, kHiddenTopic));
  EXPECT_FALSE(contains(response->output, "Published topics:"));
  EXPECT_FALSE(contains(response->output, "Subscribed topics:"));
  EXPECT_FALSE(contains(response->output, " publisher"));
  EXPECT_FALSE(contains(response->output, " subscriber"));

  TopicListServiceOptions show_types_options;
  show_types_options.show_types = true;
  const auto show_types_response =
    callTopicListService(robotANode(), identityB(), show_types_options);
  ASSERT_NE(show_types_response, nullptr);
  EXPECT_TRUE(show_types_response->success) << show_types_response->err_msg;
  EXPECT_TRUE(
      contains(show_types_response->output,
               std::string(kBidirectionalTopic) + " [std_msgs/msg/String]"));
  EXPECT_FALSE(contains(show_types_response->output, kHiddenTopic));
  EXPECT_FALSE(contains(show_types_response->output, "Published topics:"));
  EXPECT_FALSE(contains(show_types_response->output, " publisher"));

  TopicListServiceOptions include_hidden_options;
  include_hidden_options.include_hidden_topics = true;
  const auto include_hidden_response =
    callTopicListService(robotANode(), identityB(), include_hidden_options);
  ASSERT_NE(include_hidden_response, nullptr);
  EXPECT_TRUE(include_hidden_response->success)
      << include_hidden_response->err_msg;
  EXPECT_TRUE(contains(include_hidden_response->output, kHiddenTopic));
  EXPECT_FALSE(
      contains(include_hidden_response->output, "[std_msgs/msg/String]"));

  TopicListServiceOptions verbose_options;
  verbose_options.verbose = true;
  const auto verbose_response =
    callTopicListService(robotANode(), identityB(), verbose_options);
  ASSERT_NE(verbose_response, nullptr);
  EXPECT_TRUE(verbose_response->success) << verbose_response->err_msg;
  EXPECT_TRUE(contains(verbose_response->output, "Published topics:"));
  EXPECT_TRUE(contains(verbose_response->output, "Subscribed topics:"));
  EXPECT_TRUE(contains(verbose_response->output,
                       " * /parameter_events "
                       "[rcl_interfaces/msg/ParameterEvent] 2 publishers\n"));
  EXPECT_TRUE(contains(verbose_response->output,
                       " * /rosout [rcl_interfaces/msg/Log] 2 publishers\n"));
  EXPECT_TRUE(contains(verbose_response->output,
                       " * " + std::string(kBidirectionalTopic) +
                       " [std_msgs/msg/String] 1 subscriber\n"));
  EXPECT_TRUE(
      contains(verbose_response->output,
               std::string(kBidirectionalTopic) + " [std_msgs/msg/String]"));
  EXPECT_FALSE(contains(verbose_response->output, kHiddenTopic));

  TopicListServiceOptions count_topics_options;
  count_topics_options.count_topics = true;
  const auto count_topics_response =
    callTopicListService(robotANode(), identityB(), count_topics_options);
  ASSERT_NE(count_topics_response, nullptr);
  EXPECT_TRUE(count_topics_response->success) << count_topics_response->err_msg;
  EXPECT_EQ(count_topics_response->output,
            std::to_string(lineCount(response->output)) + "\n");
  EXPECT_FALSE(contains(count_topics_response->output, "/"));
  EXPECT_FALSE(contains(count_topics_response->output, "["));
  EXPECT_FALSE(contains(count_topics_response->output, "Published topics:"));
  EXPECT_FALSE(contains(count_topics_response->output, " publisher"));

  const auto missing_response =
    callTopicListService(robotANode(), "missing-livekit-participant");
  ASSERT_NE(missing_response, nullptr);
  EXPECT_FALSE(missing_response->success);
  EXPECT_TRUE(
      contains(missing_response->err_msg, "missing-livekit-participant"));
}

TEST_F(BridgeTestE2E, ListsRemoteRosServicesOverRpc) {
  initializeRuntime(kBidirectionalTopic);

  constexpr const char *kVisibleService = "/bridge/listable_service";
  auto visible_service = robotBNode()->create_service<Ros2ServiceList>(
      kVisibleService, [](const std::shared_ptr<Ros2ServiceList::Request>,
    std::shared_ptr<Ros2ServiceList::Response> response) {
      response->success = true;
      });
  ASSERT_NE(visible_service, nullptr);
  ASSERT_TRUE(
      waitFor([&]() {return serviceExists(*robotBNode(), kVisibleService);},
              kGraphTimeout))
      << "Visible service did not appear in bridge B graph";

  constexpr const char *kHiddenService = "/_hidden_service";
  auto hidden_service = robotBNode()->create_service<Ros2ServiceList>(
      kHiddenService, [](const std::shared_ptr<Ros2ServiceList::Request>,
    std::shared_ptr<Ros2ServiceList::Response> response) {
      response->success = true;
      });
  ASSERT_NE(hidden_service, nullptr);
  ASSERT_TRUE(
      waitFor([&]() {return serviceExists(*robotBNode(), kHiddenService);},
              kGraphTimeout))
      << "Hidden service did not appear in bridge B graph";

  const auto response = callServiceListService(robotANode(), identityB());
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success) << response->err_msg;
  EXPECT_TRUE(contains(response->output, kVisibleService));
  EXPECT_FALSE(contains(response->output, "["));
  EXPECT_FALSE(contains(response->output, kHiddenService));

  ServiceListServiceOptions show_types_options;
  show_types_options.show_types = true;
  const auto show_types_response =
    callServiceListService(robotANode(), identityB(), show_types_options);
  ASSERT_NE(show_types_response, nullptr);
  EXPECT_TRUE(show_types_response->success) << show_types_response->err_msg;
  EXPECT_TRUE(contains(show_types_response->output,
                       std::string(kVisibleService) +
                           " [ros2_livekit_bridge_msgs/srv/Ros2ServiceList]"));
  EXPECT_FALSE(contains(show_types_response->output, kHiddenService));

  ServiceListServiceOptions include_hidden_options;
  include_hidden_options.include_hidden_services = true;
  const auto include_hidden_response =
    callServiceListService(robotANode(), identityB(), include_hidden_options);
  ASSERT_NE(include_hidden_response, nullptr);
  EXPECT_TRUE(include_hidden_response->success)
      << include_hidden_response->err_msg;
  EXPECT_TRUE(contains(include_hidden_response->output, kHiddenService));
  EXPECT_FALSE(contains(include_hidden_response->output,
                        "[ros2_livekit_bridge_msgs/srv/Ros2ServiceList]"));

  ServiceListServiceOptions count_services_options;
  count_services_options.count_services = true;
  const auto count_services_response =
    callServiceListService(robotANode(), identityB(), count_services_options);
  ASSERT_NE(count_services_response, nullptr);
  EXPECT_TRUE(count_services_response->success)
      << count_services_response->err_msg;
  EXPECT_EQ(count_services_response->output,
            std::to_string(lineCount(response->output)) + "\n");
  EXPECT_FALSE(contains(count_services_response->output, "/"));
  EXPECT_FALSE(contains(count_services_response->output, "["));

  const auto missing_response =
    callServiceListService(robotANode(), "missing-livekit-participant");
  ASSERT_NE(missing_response, nullptr);
  EXPECT_FALSE(missing_response->success);
  EXPECT_TRUE(
      contains(missing_response->err_msg, "missing-livekit-participant"));
}

TEST_F(BridgeTestE2E, CallsRemoteRosServiceOverRpc) {
  initializeRuntime(kBidirectionalTopic);

  constexpr const char *kSetBoolService = "/bridge/set_bool";
  auto set_bool_service = robotBNode()->create_service<std_srvs::srv::SetBool>(
      kSetBoolService,
    [](const std_srvs::srv::SetBool::Request::SharedPtr request,
    std_srvs::srv::SetBool::Response::SharedPtr response) {
      response->success = request->data;
      response->message = request->data ? "enabled" : "disabled";
      });
  ASSERT_NE(set_bool_service, nullptr);
  ASSERT_TRUE(
      waitFor([&]() {return serviceExists(*robotBNode(), kSetBoolService);},
              kGraphTimeout))
      << "SetBool service did not appear in bridge B graph";

  const auto response = callServiceCallService(
      robotANode(), identityB(), kSetBoolService, "std_srvs/srv/SetBool",
      "{data: true}");
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success) << response->err_msg;
  EXPECT_TRUE(contains(response->output, "success: true"));
  EXPECT_TRUE(contains(response->output, "message: enabled"));

  const auto missing_type_response = callServiceCallService(
      robotANode(), identityB(), kSetBoolService, "", "{data: false}");
  ASSERT_NE(missing_type_response, nullptr);
  EXPECT_FALSE(missing_type_response->success);
  EXPECT_TRUE(
      contains(missing_type_response->err_msg,
               "msg_type must be non-empty"));

  ServiceCallServiceOptions timeout_options;
  timeout_options.timeout_sec = 1;
  const auto missing_service_response = callServiceCallService(
      robotANode(), identityB(), "/bridge/missing_set_bool",
      "std_srvs/srv/SetBool", "{data: true}", timeout_options);
  ASSERT_NE(missing_service_response, nullptr);
  EXPECT_FALSE(missing_service_response->success);
  EXPECT_TRUE(
      contains(missing_service_response->err_msg, "Service call timed out"));

  const auto missing_participant_response = callServiceCallService(
      robotANode(), "missing-livekit-participant", kSetBoolService,
      "std_srvs/srv/SetBool", "{data: true}");
  ASSERT_NE(missing_participant_response, nullptr);
  EXPECT_FALSE(missing_participant_response->success);
  EXPECT_TRUE(
      contains(missing_participant_response->err_msg,
               "missing-livekit-participant"));
}

TEST_F(BridgeTestE2E, ShowsRemoteRosInterfacesOverRpc) {
  initializeRuntime(kBidirectionalTopic);

  constexpr const char *kInterfaceType = "std_msgs/msg/Header";
  const auto response =
    callInterfaceShowService(robotANode(), identityB(), kInterfaceType);
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success) << response->err_msg;
  EXPECT_TRUE(
      contains(response->output,
               "# Standard metadata for higher-level stamped data types."));
  EXPECT_TRUE(contains(response->output, "builtin_interfaces/Time stamp"));
  EXPECT_TRUE(contains(response->output, "\tint32 sec"));
  EXPECT_FALSE(contains(response->output,
                        "# This message communicates ROS Time defined here:"));

  InterfaceShowServiceOptions all_comments_options;
  all_comments_options.all_comments = true;
  const auto all_comments_response = callInterfaceShowService(
      robotANode(), identityB(), kInterfaceType, all_comments_options);
  ASSERT_NE(all_comments_response, nullptr);
  EXPECT_TRUE(all_comments_response->success) << all_comments_response->err_msg;
  EXPECT_TRUE(contains(all_comments_response->output,
                       "# This message communicates ROS Time defined here:"));

  InterfaceShowServiceOptions no_comments_options;
  no_comments_options.no_comments = true;
  const auto no_comments_response = callInterfaceShowService(
      robotANode(), identityB(), kInterfaceType, no_comments_options);
  ASSERT_NE(no_comments_response, nullptr);
  EXPECT_TRUE(no_comments_response->success) << no_comments_response->err_msg;
  EXPECT_TRUE(
      contains(no_comments_response->output, "builtin_interfaces/Time stamp"));
  EXPECT_TRUE(contains(no_comments_response->output, "\tuint32 nanosec"));
  EXPECT_FALSE(contains(no_comments_response->output, "#"));
  EXPECT_FALSE(contains(no_comments_response->output, "\n\n"));

  const auto invalid_type_response = callInterfaceShowService(
      robotANode(), identityB(), "missing_pkg/msg/Thing");
  ASSERT_NE(invalid_type_response, nullptr);
  EXPECT_FALSE(invalid_type_response->success);
  EXPECT_TRUE(contains(invalid_type_response->err_msg, "missing_pkg"));

  const auto missing_response = callInterfaceShowService(
      robotANode(), "missing-livekit-participant", kInterfaceType);
  ASSERT_NE(missing_response, nullptr);
  EXPECT_FALSE(missing_response->success);
  EXPECT_TRUE(
      contains(missing_response->err_msg, "missing-livekit-participant"));
}

TEST_F(BridgeTestE2E, PublishesToRemoteRosTopicOverRpc) {
  // A broader pattern lets a single connected room exercise an allowed publish
  // topic, an out-of-pattern (denied) topic, and a graph type mismatch.
  constexpr const char *kPubPattern = "/bridge/.*";
  initializeRuntime(
      kPubPattern, kPubPattern, kBidirectionalTopic, kBidirectionalTopic);

  constexpr const char *kPubTopic = "/bridge/cmd";
  constexpr const char *kStringType = "std_msgs/msg/String";
  constexpr const char *kPayload = "hello-from-e2e";

  std::mutex mutex;
  std::optional<std::string> received;
  auto subscription = robotBNode()->create_subscription<std_msgs::msg::String>(
      kPubTopic, 10,
    [&](const std_msgs::msg::String::ConstSharedPtr message) {
      if (message->data != kPayload) {
        return;
      }
      std::lock_guard<std::mutex> lock(mutex);
      received = message->data;
      });
  ASSERT_TRUE(
      waitFor([&]() {return topicExists(*robotBNode(), kPubTopic);},
              kGraphTimeout))
      << "Subscriber topic did not appear in bridge B graph";

  // `ros2 topic pub` is one-shot, so the remote generic publisher only
  // delivers once it has matched the subscriber. Republish until the payload
  // arrives or the message timeout elapses.
  Ros2TopicPubSrv::Response::SharedPtr response;
  const bool delivered = waitFor(
    [&]() {
      response = callTopicPubService(
            robotANode(), identityB(), kPubTopic, kStringType,
            std::string("{data: ") + kPayload + "}");
      if (response == nullptr || !response->success) {
        return false;
      }
      std::lock_guard<std::mutex> lock(mutex);
      return received.has_value();
      },
      kMessageTimeout);
  subscription.reset();

  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success) << response->err_msg;
  EXPECT_TRUE(delivered)
      << "Remote bridge did not deliver the published payload";

  // A topic outside the configured incoming patterns must be rejected by the
  // remote bridge before any publisher is created.
  const auto denied_response = callTopicPubService(
      robotANode(), identityB(), "/forbidden/topic", kStringType,
      std::string("{data: ") + kPayload + "}");
  ASSERT_NE(denied_response, nullptr);
  EXPECT_FALSE(denied_response->success);
  EXPECT_TRUE(contains(denied_response->err_msg, "/forbidden/topic"));
  EXPECT_TRUE(contains(denied_response->err_msg, "not allowed"));

  // The bidirectional topic is already in bridge B's graph as a String, so a
  // mismatched requested type must be rejected against the known graph type.
  const auto type_mismatch_response = callTopicPubService(
      robotANode(), identityB(), kBidirectionalTopic, "std_msgs/msg/Int32",
      "{data: 1}");
  ASSERT_NE(type_mismatch_response, nullptr);
  EXPECT_FALSE(type_mismatch_response->success);
  EXPECT_TRUE(contains(type_mismatch_response->err_msg, kBidirectionalTopic));
  EXPECT_TRUE(contains(type_mismatch_response->err_msg, "std_msgs/msg/String"));

  const auto missing_response = callTopicPubService(
      robotANode(), "missing-livekit-participant", kPubTopic, kStringType,
      std::string("{data: ") + kPayload + "}");
  ASSERT_NE(missing_response, nullptr);
  EXPECT_FALSE(missing_response->success);
  EXPECT_TRUE(
      contains(missing_response->err_msg, "missing-livekit-participant"));
}

} // namespace
} // namespace ros2_livekit_bridge::test
