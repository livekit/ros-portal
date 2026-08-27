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

#include "ros_portal/token_loader.hpp"

#include <exception>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <utility>

#include "ros_portal/utils/ros_utils.hpp"

namespace ros_portal {
namespace {

constexpr char kTokenLoaderLoggerName[] = "ros_portal.token";

} // namespace

TokenLoader::TokenLoader()
    : logger_(rclcpp::get_logger(kTokenLoaderLoggerName)),
      livekit_url_(utils::environmentVariable("LIVEKIT_URL")),
      token_(utils::environmentVariable("LIVEKIT_TOKEN")),
      endpoint_(utils::environmentVariable("LIVEKIT_TOKEN_ENDPOINT")),
      server_id_(utils::environmentVariable("LIVEKIT_TOKEN_SERVER_ID")) {}

bool TokenLoader::valid() const {
  const bool has_token = token_.has_value();
  const bool has_endpoint = endpoint_.has_value();
  const bool has_server_id = server_id_.has_value();
  return (has_token || has_endpoint || has_server_id) && !(has_token && has_endpoint) &&
         !(has_token && has_server_id) && !(has_endpoint && has_server_id);
}

std::optional<livekit::TokenSourceResponse> TokenLoader::load() const {
  RCLCPP_DEBUG(logger_, "Attempting to resolve LiveKit token source");

  if (!valid()) {
    RCLCPP_ERROR(logger_,
                 "Invalid LiveKit token source configuration: set exactly one of LIVEKIT_TOKEN, "
                 "LIVEKIT_TOKEN_ENDPOINT, or LIVEKIT_TOKEN_SERVER_ID");
    return std::nullopt;
  }
  livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError> result =
      livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>::failure(
          livekit::TokenSourceError{"unsupported token source"});
  try {
    if (token_) {
      if (!livekit_url_) {
        RCLCPP_ERROR(logger_,
                     "Invalid LiveKit token source configuration: LIVEKIT_TOKEN is configured but LIVEKIT_URL is "
                     "missing");
        return std::nullopt;
      }
      RCLCPP_INFO(logger_, "Using LiveKit literal token source");
      auto source = livekit::LiteralTokenSource::create(*livekit_url_, *token_);
      result = source->fetch().get();
    } else if (endpoint_) {
      RCLCPP_INFO(logger_, "Using LiveKit endpoint token source");
      auto source = livekit::EndpointTokenSource::create(*endpoint_);
      result = source->fetch().get();
    } else {
      RCLCPP_INFO(logger_, "Using LiveKit development token server source");
      auto source = livekit::DevelopmentTokenSource::create(*server_id_);
      result = source->fetch().get();
    }
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger_, "Failed to fetch LiveKit credentials: %s", error.what());
    return std::nullopt;
  }

  if (!result) {
    RCLCPP_ERROR(logger_, "Failed to fetch LiveKit credentials: %s", result.error().message.c_str());
    return std::nullopt;
  }
  return std::move(result).value();
}

} // namespace ros_portal
