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

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <utility>

namespace ros_portal {
namespace {

constexpr char kTokenLoaderLoggerName[] = "ros_portal.token_loader";

std::string environmentValue(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string{value};
}

} // namespace

TokenLoader::TokenLoader() {
  const std::string token = environmentValue("LIVEKIT_TOKEN");
  const std::string endpoint = environmentValue("LIVEKIT_TOKEN_ENDPOINT");
  const std::string server_id = environmentValue("LIVEKIT_TOKEN_SERVER_ID");
  const std::size_t source_count = static_cast<std::size_t>(!token.empty()) + static_cast<std::size_t>(!endpoint.empty()) +
                                   static_cast<std::size_t>(!server_id.empty());

  if (source_count > 1U) {
    configuration_error_ = "multiple token sources are configured; set exactly one of LIVEKIT_TOKEN, "
                           "LIVEKIT_TOKEN_ENDPOINT, or LIVEKIT_TOKEN_SERVER_ID";
    return;
  }
  if (source_count == 0U) {
    configuration_error_ = "no token source is configured; set LIVEKIT_TOKEN (with LIVEKIT_URL), "
                           "LIVEKIT_TOKEN_ENDPOINT, or LIVEKIT_TOKEN_SERVER_ID";
    return;
  }
  livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError> result =
      livekit::Result<livekit::TokenSourceResponse, livekit::TokenSourceError>::failure(
          livekit::TokenSourceError{"unsupported token source"});
  try {
    if (!token.empty()) {
      const std::string server_url = environmentValue("LIVEKIT_URL");
      if (server_url.empty()) {
        configuration_error_ = "LIVEKIT_TOKEN is configured but LIVEKIT_URL is missing";
        return;
      }
      auto source = livekit::LiteralTokenSource::create(server_url, token);
      result = source->fetch().get();
    } else if (!endpoint.empty()) {
      auto source = livekit::EndpointTokenSource::create(endpoint);
      result = source->fetch().get();
    } else {
      auto source = livekit::DevelopmentTokenSource::create(server_id);
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
