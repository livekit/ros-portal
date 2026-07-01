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

/// @brief Shared helpers for ROS introspection metadata and runtime messages.

#pragma once

#include <rosidl_runtime_c/message_type_support_struct.h>

#include <optional>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <string>

namespace ros2_livekit_bridge::introspection {

/// @brief Extract message metadata from an introspection type-support handle.
///
/// The introspection type-support handle carries its MessageMembers as a
/// type-erased `void *`; this is the single checked cast for that access.
///
/// @param handle Introspection message type-support handle.
/// @return Pointer to the message members, or nullptr when the handle or its
///   payload is null.
inline const rosidl_typesupport_introspection_cpp::MessageMembers *membersFromHandle(
    const rosidl_message_type_support_t *handle) {
  if (handle == nullptr || handle->data == nullptr) {
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(handle->data);
}

/// @brief Build a ROS interface type string from message introspection metadata.
/// @param members Introspection metadata for the message type.
/// @return ROS interface type, such as `std_msgs/msg/String`.
std::string messageTypeString(const rosidl_typesupport_introspection_cpp::MessageMembers &members);

/// @brief Format a runtime message as YAML.
/// @param msg_type ROS interface type, such as `std_srvs/srv/SetBool_Response`.
/// @param message Pointer to the runtime message memory.
/// @return YAML rendering of @p message.
std::string toYaml(const std::string &msg_type, const void *message);

/// @brief Format a runtime message as YAML.
/// @param members Introspection metadata for the message type.
/// @param message Pointer to the runtime message memory.
/// @return YAML rendering of @p message.
std::string toYaml(const rosidl_typesupport_introspection_cpp::MessageMembers &members, const void *message);

/// @brief Convert a native `ros2 topic pub` YAML payload to serialized ROS CDR.
/// @param msg_type ROS interface type, such as `std_msgs/msg/String`.
/// @param payload YAML message payload.
/// @param error Set to a human-readable description when conversion fails.
/// @return Serialized ROS message bytes, or `std::nullopt` when the type cannot
///   be resolved or the payload is invalid.
std::optional<rclcpp::SerializedMessage> serializedMessageFromYaml(const std::string &msg_type,
                                                                   const std::string &payload, std::string &error);

/// @brief Populate an existing runtime message from a native YAML payload.
/// @param msg_type ROS interface type, such as `std_srvs/srv/SetBool_Request`.
/// @param payload YAML message payload.
/// @param message Type-erased initialized message storage to populate.
/// @param error Set to a human-readable description when conversion fails.
/// @return True when @p message was populated successfully.
bool populateMessageFromYaml(const std::string &msg_type, const std::string &payload, void *message,
                             std::string &error);

} // namespace ros2_livekit_bridge::introspection
