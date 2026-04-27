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

#ifndef ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_
#define ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_

#include <regex>
#include <string>
#include <vector>

namespace livekit::ros_bridge::utils
{

struct PatternCompileError
{
  std::string pattern;
  std::string message;
};

std::vector<std::regex> compileRegexPatterns(
  const std::vector<std::string> & patterns,
  std::vector<PatternCompileError> *errors = nullptr);

bool matchesAnyPattern(
  const std::string & value,
  const std::vector<std::regex> & patterns);

} // namespace livekit::ros_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_
