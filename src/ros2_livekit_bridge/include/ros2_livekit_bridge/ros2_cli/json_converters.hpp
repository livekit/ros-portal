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

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ros2_livekit_bridge/ros2_cli/types.hpp"

namespace ros2_livekit_bridge
{

/**
 * @brief Convert a ROS service request to local topic-list options.
 * @param request ROS service request.
 * @return Topic-list options from the request flags.
 */
TopicListOptions topicListOptionsFromRequest(
  const ros2_cli::Ros2TopicList::Request & request);

/**
 * @brief Convert a ROS service request to local topic-pub options.
 * @param request ROS service request.
 * @return Topic-pub options from the request fields.
 */
TopicPubOptions topicPubOptionsFromRequest(
  const ros2_cli::Ros2TopicPub::Request & request);

/**
 * @brief Convert a ROS service request to local service-list options.
 * @param request ROS service request.
 * @return Service-list options from the request flags.
 */
ServiceListOptions serviceListOptionsFromRequest(
  const ros2_cli::Ros2ServiceList::Request & request);

/**
 * @brief Convert a ROS service request to local interface-show options.
 * @param request ROS service request.
 * @return Interface-show options from the request fields.
 */
InterfaceShowOptions interfaceShowOptionsFromRequest(
  const ros2_cli::Ros2InterfaceShow::Request & request);

/**
 * @brief Serialize a ROS service request as a LiveKit RPC JSON payload.
 * @param request ROS service request.
 * @param timeout_sec Effective timeout to include in the payload.
 * @return JSON request payload.
 */
std::string topicListRequestToJson(
  const ros2_cli::Ros2TopicList::Request & request,
  std::uint8_t timeout_sec);

/**
 * @brief Serialize a ROS service request as a LiveKit RPC JSON payload.
 * @param request ROS service request.
 * @param timeout_sec Effective timeout to include in the payload.
 * @return JSON request payload.
 */
std::string topicPubRequestToJson(
  const ros2_cli::Ros2TopicPub::Request & request,
  std::uint8_t timeout_sec);

/**
 * @brief Serialize a ROS service request as a LiveKit RPC JSON payload.
 * @param request ROS service request.
 * @param timeout_sec Effective timeout to include in the payload.
 * @return JSON request payload.
 */
std::string serviceListRequestToJson(
  const ros2_cli::Ros2ServiceList::Request & request,
  std::uint8_t timeout_sec);

/**
 * @brief Serialize a ROS service request as a LiveKit RPC JSON payload.
 * @param request ROS service request.
 * @param timeout_sec Effective timeout to include in the payload.
 * @return JSON request payload.
 */
std::string interfaceShowRequestToJson(
  const ros2_cli::Ros2InterfaceShow::Request & request,
  std::uint8_t timeout_sec);

/**
 * @brief Parse topic-list options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @param error Set to a description when @p payload is malformed.
 * @return Topic-list options, or `std::nullopt` when @p payload is malformed.
 */
std::optional<TopicListOptions> topicListOptionsFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse topic-pub options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @param error Set to a description when @p payload is malformed or invalid.
 * @return Topic-pub options, or `std::nullopt` when @p payload is invalid.
 */
std::optional<TopicPubOptions> topicPubOptionsFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse service-list options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @param error Set to a description when @p payload is malformed.
 * @return Service-list options, or `std::nullopt` when @p payload is malformed.
 */
std::optional<ServiceListOptions> serviceListOptionsFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse interface-show options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @param error Set to a description when @p payload is malformed.
 * @return Interface-show options, or `std::nullopt` when @p payload is malformed.
 */
std::optional<InterfaceShowOptions> interfaceShowOptionsFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Construct a ROS service response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return ROS service response.
 */
ros2_cli::Ros2TopicList::Response makeTopicListResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output = {});

/**
 * @brief Construct a ROS service response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return ROS service response.
 */
ros2_cli::Ros2TopicPub::Response makeTopicPubResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output = {});

/**
 * @brief Construct a ROS service response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return ROS service response.
 */
ros2_cli::Ros2ServiceList::Response makeServiceListResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output = {});

/**
 * @brief Construct a ROS service response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return ROS service response.
 */
ros2_cli::Ros2InterfaceShow::Response makeInterfaceShowResponse(
  bool success,
  const std::string & err_msg,
  const std::string & output = {});

/**
 * @brief Serialize a topic-list result as a LiveKit RPC JSON response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return JSON response payload.
 */
std::string topicListResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output);

/**
 * @brief Serialize a topic-pub result as a LiveKit RPC JSON response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return JSON response payload.
 */
std::string topicPubResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output);

/**
 * @brief Serialize a service-list result as a LiveKit RPC JSON response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return JSON response payload.
 */
std::string serviceListResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output);

/**
 * @brief Serialize an interface-show result as a LiveKit RPC JSON response.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return JSON response payload.
 */
std::string interfaceShowResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @param error Set to a description when @p payload is malformed or incomplete.
 * @return ROS service response, or `std::nullopt` when @p payload is invalid.
 */
std::optional<ros2_cli::Ros2TopicList::Response> topicListResponseFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @param error Set to a description when @p payload is malformed or incomplete.
 * @return ROS service response, or `std::nullopt` when @p payload is invalid.
 */
std::optional<ros2_cli::Ros2TopicPub::Response> topicPubResponseFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @param error Set to a description when @p payload is malformed or incomplete.
 * @return ROS service response, or `std::nullopt` when @p payload is invalid.
 */
std::optional<ros2_cli::Ros2ServiceList::Response> serviceListResponseFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @param error Set to a description when @p payload is malformed or incomplete.
 * @return ROS service response, or `std::nullopt` when @p payload is invalid.
 */
std::optional<ros2_cli::Ros2InterfaceShow::Response>
interfaceShowResponseFromJson(
  const std::string & payload,
  std::string & error);

}  // namespace ros2_livekit_bridge
