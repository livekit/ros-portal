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

#include <cstdlib>
#include <exception>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace ros_portal {
namespace {

constexpr char kTokenLoaderLoggerName[] = "ros_portal.token";

std::optional<std::string> environmentVariable(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string{value};
}

} // namespace

TokenLoader::TokenLoader() {
  RCLCPP_DEBUG(rclcpp::get_logger(kTokenLoaderLoggerName), "Attempting to resolve LiveKit token source");

  const auto token = environmentVariable("LIVEKIT_TOKEN");
  const auto endpoint = environmentVariable("LIVEKIT_TOKEN_ENDPOINT");
  const auto server_id = environmentVariable("LIVEKIT_TOKEN_SERVER_ID");
  const bool has_literal_token = token.has_value();
  const bool has_endpoint = endpoint.has_value();
  const bool has_development_server = server_id.has_value();

  if ((has_literal_token && has_endpoint) || (has_literal_token && has_development_server) ||
      (has_endpoint && has_development_server)) {
    configuration_error_ = "multiple token sources are configured; set exactly one of LIVEKIT_TOKEN, "
                           "LIVEKIT_TOKEN_ENDPOINT, or LIVEKIT_TOKEN_SERVER_ID";
    return;
  }
  if (!has_literal_token && !has_endpoint && !has_development_server) {
    configuration_error_ = "no token source is configured; set LIVEKIT_TOKEN (with LIVEKIT_URL), "
                           "LIVEKIT_TOKEN_ENDPOINT, or LIVEKIT_TOKEN_SERVER_ID";
    return;
  }
  livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError> result =
      livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>::failure(
          livekit::TokenSourceError{"unsupported token source"});
  try {
    if (has_literal_token) {
      const auto server_url = environmentVariable("LIVEKIT_URL");
      if (!server_url) {
        configuration_error_ = "LIVEKIT_TOKEN is configured but LIVEKIT_URL is missing";
        return;
      }
      RCLCPP_INFO(rclcpp::get_logger(kTokenLoaderLoggerName), "Using LiveKit literal token source");
      auto source = livekit::LiteralTokenSource::create(*server_url, *token);
      result = source->fetch().get();
    } else if (has_endpoint) {
      RCLCPP_INFO(rclcpp::get_logger(kTokenLoaderLoggerName), "Using LiveKit endpoint token source");
      auto source = livekit::EndpointTokenSource::create(*endpoint);
      result = source->fetch().get();
    } else {
      RCLCPP_INFO(rclcpp::get_logger(kTokenLoaderLoggerName), "Using LiveKit development token server source");
      auto source = livekit::DevelopmentTokenSource::create(*server_id);
      result = source->fetch().get();
    }
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger(kTokenLoaderLoggerName), "Failed to fetch LiveKit credentials: %s", error.what());
    return;
  }

  if (!result) {
    RCLCPP_ERROR(rclcpp::get_logger(kTokenLoaderLoggerName), "Failed to fetch LiveKit credentials: %s",
                 result.error().message.c_str());
    return;
  }
  credentials_ = std::move(result).value();
}

bool TokenLoader::valid() const {
  if (!configuration_error_.empty()) {
    RCLCPP_ERROR(rclcpp::get_logger(kTokenLoaderLoggerName), "Invalid LiveKit token source configuration: %s",
                 configuration_error_.c_str());
    return false;
  }
  return credentials_.has_value();
}

const livekit::TokenSourceResponse& TokenLoader::get() const {
  if (!credentials_) {
    throw std::logic_error("TokenLoader::get() called without loaded credentials");
  }
  return *credentials_;
}

} // namespace ros_portal
