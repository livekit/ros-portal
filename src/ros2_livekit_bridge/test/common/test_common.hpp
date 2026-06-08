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

#include <cstdlib>
#include <optional>
#include <string>

namespace ros2_livekit_bridge::test
{

inline std::optional<std::string> getenvString(const char * name)
{
  const char * value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

inline bool setEnv(const char * name, const std::string & value)
{
  return ::setenv(name, value.c_str(), 1) == 0;
}

inline void restoreEnv(const char * name, const std::optional<std::string> & value)
{
  if (value) {
    (void)::setenv(name, value->c_str(), 1);
  } else {
    (void)::unsetenv(name);
  }
}

}  // namespace ros2_livekit_bridge::test
