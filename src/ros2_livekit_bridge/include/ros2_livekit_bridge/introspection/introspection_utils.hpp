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

/// @brief Shared low-level helpers for walking ROS introspection metadata.
///
/// These are the single source of truth for the type-erased pointer arithmetic
/// and `void *` casts that the introspection message renderer and YAML
/// converter both rely on. Keeping them here means there is exactly one place
/// to audit for the unavoidable casts the ROS 2 introspection C API forces.

#pragma once

#include <rosidl_runtime_c/message_type_support_struct.h>

#include <cstdint>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::introspection {

/// @brief Return a const pointer to a member inside a message buffer.
/// @param message Pointer to the start of the message memory.
/// @param member Introspection metadata describing the member offset.
/// @return Pointer to @p member inside @p message.
inline const void *memberMemory(const void *message,
                                const rosidl_typesupport_introspection_cpp::MessageMember &member) {
  return static_cast<const void *>(static_cast<const std::uint8_t *>(message) + member.offset_);
}

/// @brief Return a mutable pointer to a member inside a message buffer.
/// @param message Pointer to the start of the message memory.
/// @param member Introspection metadata describing the member offset.
/// @return Pointer to @p member inside @p message.
inline void *memberMemory(void *message, const rosidl_typesupport_introspection_cpp::MessageMember &member) {
  return static_cast<void *>(static_cast<std::uint8_t *>(message) + member.offset_);
}

/// @brief Extract nested message metadata from a message-typed member.
///
/// The ROS 2 introspection C API exposes nested-message metadata as a
/// type-erased `void *` on the member handle, so this cast is unavoidable.
/// Keeping it here means there is exactly one place to audit the raw pointer.
///
/// @param member Introspection metadata for a message-typed field.
/// @return Pointer to the nested members, or nullptr when metadata is missing.
inline const rosidl_typesupport_introspection_cpp::MessageMembers *nestedMembers(
    const rosidl_typesupport_introspection_cpp::MessageMember &member) {
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(member.members_->data);
}

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

} // namespace ros2_livekit_bridge::introspection
