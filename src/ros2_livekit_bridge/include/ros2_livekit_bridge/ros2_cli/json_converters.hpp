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
#include <string>

#include "ros2_livekit_bridge/ros2_cli/types.hpp"

namespace ros2_livekit_bridge
{

/**
 * @brief Formatting and graph filtering options for `ros2 topic list`.
 * @details fields map 1:1 to the fields shown from running `ros2 topic list --help`
 */
struct TopicListOptions
{
  //! @brief Render topic types next to each topic name.
  bool show_types{false};
  //! @brief Render only the number of discovered topics.
  bool count_topics{false};
  //! @brief Include hidden topics whose names contain hidden tokens.
  bool include_hidden_topics{false};
  //! @brief Render full publisher/subscriber details.
  bool verbose{false};
};

/**
 * @brief Formatting and graph filtering options for `ros2 service list`.
 * @details fields map 1:1 to the fields shown from running `ros2 service list --help`
 */
struct ServiceListOptions
{
  //! @brief Render service types next to each service name.
  bool show_types{false};
  //! @brief Render only the number of discovered services.
  bool count_services{false};
  //! @brief Include hidden services whose names contain hidden tokens.
  bool include_hidden_services{false};
};

/**
 * @brief Arguments and formatting options for `ros2 interface show`.
 * @details fields map 1:1 to the fields shown from running `ros2 interface show --help`
 */
struct InterfaceShowOptions
{
  //! @brief Interface type identifier to show, such as `std_msgs/msg/String`.
  std::string type;
  //! @brief Show all comments, including nested interface comments.
  bool all_comments{false};
  //! @brief Hide comments and whitespace.
  bool no_comments{false};
};

/**
 * @brief Convert a ROS service request to local topic-list options.
 * @param request ROS service request.
 * @return Topic-list options from the request flags.
 */
TopicListOptions topicListOptionsFromRequest(
  const ros2_cli::Ros2TopicList::Request & request);

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
 * @return Topic-list options from the payload flags.
 * @throws nlohmann::json::exception when @p payload is malformed.
 */
TopicListOptions topicListOptionsFromJson(const std::string & payload);

/**
 * @brief Parse service-list options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @return Service-list options from the payload flags.
 * @throws nlohmann::json::exception when @p payload is malformed.
 */
ServiceListOptions serviceListOptionsFromJson(const std::string & payload);

/**
 * @brief Parse interface-show options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @return Interface-show options from the payload fields.
 * @throws nlohmann::json::exception when @p payload is malformed.
 */
InterfaceShowOptions interfaceShowOptionsFromJson(const std::string & payload);

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
 * @return ROS service response.
 * @throws nlohmann::json::exception when @p payload is malformed or incomplete.
 */
ros2_cli::Ros2TopicList::Response topicListResponseFromJson(const std::string & payload);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @return ROS service response.
 * @throws nlohmann::json::exception when @p payload is malformed or incomplete.
 */
ros2_cli::Ros2ServiceList::Response serviceListResponseFromJson(
  const std::string & payload);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 * @param payload JSON response payload.
 * @return ROS service response.
 * @throws nlohmann::json::exception when @p payload is malformed or incomplete.
 */
ros2_cli::Ros2InterfaceShow::Response interfaceShowResponseFromJson(
  const std::string & payload);

}  // namespace ros2_livekit_bridge
