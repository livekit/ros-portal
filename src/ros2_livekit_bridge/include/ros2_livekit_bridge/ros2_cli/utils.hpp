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

#include <string>
#include <vector>

namespace ros2_livekit_bridge::ros2_cli
{

/**
 * @brief Detect ROS hidden-name tokens in a slash-delimited graph name.
 * @param name Topic or service name to inspect.
 * @return True when any non-empty token starts with `_`.
 */
bool hasHiddenNameToken(const std::string & name);

/**
 * @brief Join ROS interface type names for CLI display.
 * @param types Interface type names associated with one graph entity.
 * @return Comma-and-space separated type list.
 *
 * Example:
 *   Type 1: std_msgs/msg/String
 *   Type 2: std_msgs/msg/Header
 *   Joined: std_msgs/msg/String, std_msgs/msg/Header
 */
std::string joinTypes(const std::vector<std::string> & types);

}  // namespace ros2_livekit_bridge::ros2_cli
