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

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

namespace ros2_livekit_bridge::utils
{

std::vector<std::regex> compileRegexPatterns(
  const std::vector<std::string> & patterns,
  std::vector<PatternCompileError> *errors)
{
  std::vector<std::regex> compiled_patterns;
  compiled_patterns.reserve(patterns.size());

  for (const auto & pattern : patterns) {
    try {
      compiled_patterns.emplace_back(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error & e) {
      if (errors) {
        errors->push_back(PatternCompileError{pattern, e.what()});
      }
    }
  }

  return compiled_patterns;
}

bool matchesAnyPattern(
  const std::string & value,
  const std::vector<std::regex> & patterns)
{
  for (const auto & pattern : patterns) {
    if (std::regex_match(value, pattern)) {
      return true;
    }
  }
  return false;
}

} // namespace ros2_livekit_bridge::utils
