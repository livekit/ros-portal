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

#include <regex>
#include <string>
#include <vector>

namespace ros_portal::utils {

struct PatternCompileError {
  std::string pattern;
  std::string message;
};

std::vector<std::regex> compileRegexPatterns(const std::vector<std::string>& patterns,
                                             std::vector<PatternCompileError>* errors = nullptr);

bool matchesAnyPattern(const std::string& value, const std::vector<std::regex>& patterns);

} // namespace ros_portal::utils
