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

/// @brief RAII storage for runtime-typed ROS messages.

#pragma once

#include <new>

#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

/// @brief Owns one introspection-backed ROS message instance.
class DynamicMessage
{
public:
  /// @brief Allocate and initialize storage for a runtime message type.
  /// @param members Introspection metadata for the message type.
  /// @param initialization ROS message initialization policy.
  explicit DynamicMessage(
    const rosidl_typesupport_introspection_cpp::MessageMembers & members,
    rosidl_runtime_cpp::MessageInitialization initialization =
      rosidl_runtime_cpp::MessageInitialization::ALL)
  : members_(members),
    data_(::operator new(members.size_of_))
  {
    try {
      members_.init_function(data_, initialization);
    } catch (...) {
      ::operator delete(data_);
      data_ = nullptr;
      throw;
    }
  }

  DynamicMessage(const DynamicMessage &) = delete;
  DynamicMessage & operator=(const DynamicMessage &) = delete;

  /// @brief Finalize and free message storage.
  ~DynamicMessage()
  {
    if (data_ != nullptr) {
      members_.fini_function(data_);
      ::operator delete(data_);
    }
  }

  /// @brief Access mutable message storage.
  void * data() {return data_;}

  /// @brief Access mutable message storage.
  void * get() {return data_;}

private:
  /// @brief Introspection members used for init/fini.
  const rosidl_typesupport_introspection_cpp::MessageMembers & members_;
  /// @brief Allocated message memory.
  void * data_;
};

}  // namespace ros2_livekit_bridge::ros2_cli
