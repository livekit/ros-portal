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

#include <functional>
#include <string>

namespace ros2_livekit_bridge
{

/**
 * @brief Handler for inbound LiveKit RPC payloads.
 *
 * The input and return value are JSON strings. Bridge transport code wraps
 * this callback in the LiveKit SDK-specific RPC handler signature.
 */
using RpcHandler = std::function<std::string(const std::string &)>;

}  // namespace ros2_livekit_bridge
