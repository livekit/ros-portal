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

#include <cstdint>

namespace ros2_livekit_bridge::ros2_cli
{

//! @brief LiveKit RPC method for remote `ros2 topic list`.
inline constexpr const char * kTopicListRpcMethod = "ros2_topic_list";
//! @brief ROS service that forwards `ros2 topic list` over LiveKit RPC.
inline constexpr const char * kTopicListServiceName =
  "/ros2_livekit_bridge/ros2_topic_list";

//! @brief LiveKit RPC method for remote `ros2 service list`.
inline constexpr const char * kServiceListRpcMethod = "ros2_service_list";
//! @brief ROS service that forwards `ros2 service list` over LiveKit RPC.
inline constexpr const char * kServiceListServiceName =
  "/ros2_livekit_bridge/ros2_service_list";

//! @brief LiveKit RPC method for remote `ros2 interface show`.
inline constexpr const char * kInterfaceShowRpcMethod = "ros2_interface_show";
//! @brief ROS service that forwards `ros2 interface show` over LiveKit RPC.
inline constexpr const char * kInterfaceShowServiceName =
  "/ros2_livekit_bridge/ros2_interface_show";

//! @brief Default LiveKit RPC timeout when a request leaves timeout_sec at zero.
inline constexpr std::uint8_t kDefaultTimeoutSec = 10;

}  // namespace ros2_livekit_bridge::ros2_cli
