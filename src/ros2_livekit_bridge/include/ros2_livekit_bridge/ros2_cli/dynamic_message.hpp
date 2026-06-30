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

#include <memory>
#include <new>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::ros2_cli {

/// @brief Owns one introspection-backed ROS message instance.
class DynamicMessage {
public:
  /// @brief Allocate and initialize storage for a runtime message type.
  /// @param members Introspection metadata for the message type.
  /// @param initialization ROS message initialization policy.
  explicit DynamicMessage(
      const rosidl_typesupport_introspection_cpp::MessageMembers &members,
      rosidl_runtime_cpp::MessageInitialization initialization = rosidl_runtime_cpp::MessageInitialization::ALL)
      : members_(members), data_(::operator new(members.size_of_), StorageDeleter{&members_, false}) {
    try {
      members_.init_function(data_.get(), initialization);
      data_.get_deleter().initialized = true;
    } catch (...) {
      data_.reset();
      throw;
    }
  }

  DynamicMessage(const DynamicMessage &) = delete;
  DynamicMessage &operator=(const DynamicMessage &) = delete;
  DynamicMessage(DynamicMessage &&) noexcept = default;
  DynamicMessage &operator=(DynamicMessage &&) = delete;

  /// @brief Access the type-erased message storage.
  ///
  /// The buffer is intentionally untyped: a dynamic message only knows its
  /// concrete type at runtime via introspection metadata, and every consumer
  /// (de/serialization, rcl send/take, YAML rendering) operates on `void *`.
  void *data() { return data_.get(); }

private:
  /// @brief Deleter that runs ROS fini before freeing untyped message storage.
  struct StorageDeleter {
    /// @brief Introspection members used for fini.
    const rosidl_typesupport_introspection_cpp::MessageMembers *members{nullptr};
    /// @brief Whether init_function completed successfully.
    bool initialized{false};

    /// @brief Finalize initialized storage, then release raw memory.
    void operator()(void *ptr) const noexcept {
      if (ptr == nullptr) {
        return;
      }
      if (initialized && members != nullptr) {
        members->fini_function(ptr);
      }
      ::operator delete(ptr);
    }
  };

  using StoragePtr = std::unique_ptr<void, StorageDeleter>;

  /// @brief Introspection members used for init/fini.
  const rosidl_typesupport_introspection_cpp::MessageMembers &members_;
  /// @brief Allocated message memory.
  StoragePtr data_;
};

} // namespace ros2_livekit_bridge::ros2_cli
