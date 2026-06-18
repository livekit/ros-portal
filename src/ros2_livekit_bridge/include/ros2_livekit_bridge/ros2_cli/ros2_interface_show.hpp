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

#include "ros2_livekit_bridge/ros2_cli/json_converters.hpp"

namespace ros2_livekit_bridge::ros2_cli
{

/**
 * @brief Render the definition for a ROS interface type.
 * @param options Interface type and comment filtering options.
 * @return Text equivalent to `ros2 interface show`, including nested message
 * definitions when referenced by the root interface.
 * @throws std::runtime_error when the type is empty, unsupported, malformed, or
 * cannot be found in the ament index.
 */
std::string renderInterfaceDefinition(const InterfaceShowOptions & options);

} // namespace ros2_livekit_bridge::ros2_cli
