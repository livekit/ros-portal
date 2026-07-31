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

#include <cstddef>
#include <cstdint>

namespace ros_portal::cli {

/// @brief LiveKit RPC method for remote `ros2 topic list`.
inline constexpr const char* kTopicListRpcMethod = "ros2_topic_list";
/// @brief ROS service that forwards `ros2 topic list` over LiveKit RPC.
inline constexpr const char* kTopicListServiceName = "ros_portal/ros2_topic_list";

/// @brief LiveKit RPC method for remote `ros2 topic pub`.
inline constexpr const char* kTopicPubRpcMethod = "ros2_topic_pub";
/// @brief ROS service that forwards `ros2 topic pub` over LiveKit RPC.
inline constexpr const char* kTopicPubServiceName = "ros_portal/ros2_topic_pub";

/// @brief LiveKit RPC method for remote `ros2 service list`.
inline constexpr const char* kServiceListRpcMethod = "ros2_service_list";
/// @brief ROS service that forwards `ros2 service list` over LiveKit RPC.
inline constexpr const char* kServiceListServiceName = "ros_portal/ros2_service_list";

/// @brief LiveKit RPC method for remote `ros2 service call`.
inline constexpr const char* kServiceCallRpcMethod = "ros2_service_call";
/// @brief ROS service that forwards `ros2 service call` over LiveKit RPC.
inline constexpr const char* kServiceCallServiceName = "ros_portal/ros2_service_call";

/// @brief LiveKit RPC method for remote `ros2 interface show`.
inline constexpr const char* kInterfaceShowRpcMethod = "ros2_interface_show";
/// @brief ROS service that forwards `ros2 interface show` over LiveKit RPC.
inline constexpr const char* kInterfaceShowServiceName = "ros_portal/ros2_interface_show";

/// @brief Default LiveKit RPC timeout when a request leaves timeout_sec at
/// zero.
inline constexpr std::uint8_t kDefaultTimeoutSec = 10;

/// @brief Extra LiveKit RPC timeout margin for remote `ros2 service call`.
/// The RPC round-trip must outlive the remote ROS service-call wait.
inline constexpr std::uint8_t kServiceCallRpcTimeoutMarginSec = 1;

/// @brief Maximum number of reusable generic topic publishers cached by topic.
inline constexpr std::size_t kMaxCachedTopicPublishers = 20U;
/// @brief Maximum number of reusable generic service clients cached by service.
inline constexpr std::size_t kMaxCachedServiceClients = 20U;
/// @brief QoS depth used for one-shot generic topic publishers.
inline constexpr std::size_t kTopicPublisherHistoryDepth = 10U;

/// @brief Maximum accepted byte length of a `ros2 topic pub` YAML payload.
inline constexpr std::size_t kMaxYamlPayloadBytes = 256U * 1024U;

/// @brief Maximum element count accepted for a single resizable ROS sequence.
inline constexpr std::size_t kMaxResizableSequenceLength = 65536U;

} // namespace ros_portal::cli
