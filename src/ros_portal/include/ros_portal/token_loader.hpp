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

#ifndef ROS_PORTAL__TOKEN_LOADER_HPP_
#define ROS_PORTAL__TOKEN_LOADER_HPP_

#include <livekit/token_source.h>

#include <optional>
#include <rclcpp/logger.hpp>
#include <string>

namespace ros_portal {

/// @brief Loads LiveKit credentials using one environment-configured token source.
class TokenLoader {
public:
  /// @brief Construct a token loader from the current environment.
  TokenLoader();

  /// @brief Return whether exactly one token source is configured.
  /// @return true when one non-empty token source environment variable is set.
  [[nodiscard]] bool valid() const;

  /// @brief Fetch credentials from the configured token source.
  /// @return Loaded LiveKit token source credentials, or std::nullopt on
  /// configuration or fetch failure.
  [[nodiscard]] std::optional<livekit::TokenSourceResponse> load() const;

private:
  /// Dedicated logger for token-source configuration and fetch diagnostics.
  rclcpp::Logger logger_;
  /// Construction-time value of `LIVEKIT_URL`.
  std::optional<std::string> livekit_url_;
  /// Construction-time value of `LIVEKIT_TOKEN`.
  std::optional<std::string> token_;
  /// Construction-time value of `LIVEKIT_TOKEN_ENDPOINT`.
  std::optional<std::string> endpoint_;
  /// Construction-time value of `LIVEKIT_TOKEN_SERVER_ID`.
  std::optional<std::string> server_id_;
};

} // namespace ros_portal

#endif // ROS_PORTAL__TOKEN_LOADER_HPP_
