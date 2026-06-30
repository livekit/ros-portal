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
#include <exception>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <livekit/result.h>
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
  const ros2_cli::Ros2TopicPubSrv::Request & request);

/**
 * @brief Convert a ROS service request to local service-list options.
 * @param request ROS service request.
 * @return Service-list options from the request flags.
 */
ServiceListOptions serviceListOptionsFromRequest(
  const ros2_cli::Ros2ServiceList::Request & request);

/// @brief Convert a ROS service request to local service-call options.
/// @param request ROS service request.
/// @return Service-call options from the request fields.
ServiceCallOptions serviceCallOptionsFromRequest(
  const ros2_cli::Ros2ServiceCallSrv::Request & request);

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
  const ros2_cli::Ros2TopicPubSrv::Request & request,
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

/// @brief Serialize a ROS service request as a LiveKit RPC JSON payload.
///
/// Ships the native YAML request payload as-is; the remote participant
/// serializes it into the request type, mirroring `ros2 topic pub`.
/// @param request ROS service request.
/// @param timeout_sec Effective timeout to include in the payload.
/// @return JSON request payload.
std::string serviceCallRequestToJson(
  const ros2_cli::Ros2ServiceCallSrv::Request & request,
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
 * @return Parsed options, or an error description when @p payload is malformed.
 */
livekit::Result<TopicListOptions, std::string> topicListOptionsFromJson(
  const std::string & payload);

/**
 * @brief Parse topic-pub options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @return Parsed options, or an error description when @p payload is invalid.
 */
livekit::Result<TopicPubOptions, std::string> topicPubOptionsFromJson(
  const std::string & payload);

/**
 * @brief Parse service-list options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @return Parsed options, or an error description when @p payload is malformed.
 */
livekit::Result<ServiceListOptions, std::string> serviceListOptionsFromJson(
  const std::string & payload);

/// @brief Parse service-call options from a LiveKit RPC JSON request payload.
/// @param payload JSON request payload.
/// @param error Set to a description when @p payload is malformed or invalid.
/// @return Service-call options, or `std::nullopt` when @p payload is invalid.
std::optional<ServiceCallOptions> serviceCallOptionsFromJson(
  const std::string & payload,
  std::string & error);

/**
 * @brief Parse interface-show options from a LiveKit RPC JSON request payload.
 * @param payload JSON request payload.
 * @return Parsed options, or an error description when @p payload is malformed.
 */
livekit::Result<InterfaceShowOptions, std::string> interfaceShowOptionsFromJson(
  const std::string & payload);

/**
 * @brief Serialize a CLI command result as a LiveKit RPC JSON response.
 *
 * Every `ros2 ...` RPC response shares the {success, err_msg, output} envelope,
 * so a single serializer covers all commands.
 * @param success Whether the operation succeeded.
 * @param err_msg Human-readable error message.
 * @param output Human-readable command output.
 * @return JSON response payload.
 */
std::string cliResponseToJson(
  bool success,
  const std::string & err_msg,
  const std::string & output);

/**
 * @brief Parse a LiveKit RPC JSON response into a ROS service response.
 *
 * Every `ros2 ...` RPC response shares the {success, err_msg, output} envelope,
 * so a single parser covers all command response types.
 * @tparam ResponseT Generated ROS service Response type.
 * @param payload JSON response payload.
 * @return Parsed response, or an error description when @p payload is invalid.
 */
template<typename ResponseT>
std::optional<ResponseT> cliResponseFromJson(
  const std::string & payload,
  std::string & error)
{
  try {
    const auto parsed = nlohmann::json::parse(payload);
    error.clear(); // success, clear any previous error
    return makeCliResponse<ResponseT>(
      parsed.at("success").template get<bool>(),
      parsed.at("err_msg").template get<std::string>(),
      parsed.at("output").template get<std::string>());
  } catch (const std::exception & parse_error) {
    error = parse_error.what();
    return std::nullopt;
  }
}

}  // namespace ros2_livekit_bridge
