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
#include <string>

namespace ros_portal {

/// @brief Loads LiveKit credentials using one environment-configured token source.
///
/// The constructor reads exactly one of `LIVEKIT_TOKEN`,
/// `LIVEKIT_TOKEN_ENDPOINT`, or `LIVEKIT_TOKEN_SERVER_ID`. `LIVEKIT_TOKEN`
/// additionally requires `LIVEKIT_URL`.
class TokenLoader {
public:
  /// @brief Read the environment and load credentials from its token source.
  TokenLoader();

  /// @brief Return whether construction loaded valid credentials.
  ///
  /// Logs the configuration error when environment validation failed.
  [[nodiscard]] bool valid() const;

  /// @brief Return the credentials loaded during construction.
  /// @pre `valid()` returned true.
  [[nodiscard]] const livekit::TokenSourceResponse& get() const;

private:
  std::string configuration_error_;
  std::optional<livekit::TokenSourceResponse> credentials_;
};

} // namespace ros_portal

#endif // ROS_PORTAL__TOKEN_LOADER_HPP_
