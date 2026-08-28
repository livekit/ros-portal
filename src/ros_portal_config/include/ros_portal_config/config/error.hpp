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

#ifndef ROS_PORTAL_CONFIG_CONFIG_ERROR_HPP_
#define ROS_PORTAL_CONFIG_CONFIG_ERROR_HPP_

#include <stdexcept>
#include <string>
#include <utility>

namespace ros_portal_config {

// Raised when a config document does not match the expected schema. Essentially
// a wrapper around yaml-cpp exception content and schema validation failures:
// carries the structured location (context), what was expected, and an
// optional detail (often YAML::Exception::what()) in addition to the formatted
// what() message.
class ConfigError : public std::runtime_error {
public:
  ConfigError(std::string context, std::string expected, std::string detail)
      : std::runtime_error(formatMessage(context, expected, detail)),
        context_(std::move(context)),
        expected_(std::move(expected)),
        detail_(std::move(detail)) {}

  const std::string& context() const { return context_; }
  const std::string& expected() const { return expected_; }
  const std::string& detail() const { return detail_; }

private:
  static std::string formatMessage(const std::string& context, const std::string& expected, const std::string& detail) {
    std::string message = context + ": expected " + expected;
    if (!detail.empty()) {
      message += " (" + detail + ")";
    }
    return message;
  }

  std::string context_;
  std::string expected_;
  std::string detail_;
};

} // namespace ros_portal_config

#endif // ROS_PORTAL_CONFIG_CONFIG_ERROR_HPP_
