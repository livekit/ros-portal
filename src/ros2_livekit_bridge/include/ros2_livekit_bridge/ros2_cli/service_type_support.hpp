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

#include <rosidl_runtime_c/message_type_support_struct.h>
#include <rosidl_runtime_c/service_type_support_struct.h>

#include <memory>
#include <rclcpp/serialization.hpp>
#include <rcpputils/shared_library.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <string>

namespace ros2_livekit_bridge::ros2_cli {

/// @brief Build the service type-support symbol name for @p type.
///
/// Replaces `/` characters in @p type with `__` when appending to the
/// type-support prefix.
///
/// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
/// @param typesupport_identifier Type-support identifier to build the symbol.
/// @return Symbol name to resolve in the type-support shared library.
std::string serviceTypeSupportSymbol(const std::string &type, const std::string &typesupport_identifier);

/// @brief Load service type support by symbol from the typesupport library.
///
/// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
/// @param typesupport_identifier Type-support identifier to build the symbol.
/// @param library Loaded type-support shared library to resolve the symbol in.
/// @return Service type-support handle for @p type, or nullptr when the
///   type-support symbol is not found.
const rosidl_service_type_support_t *serviceTypeSupportHandle(const std::string &type,
                                                              const std::string &typesupport_identifier,
                                                              rcpputils::SharedLibrary &library);

/// @brief Runtime type-support data for one ROS message type.
///
/// Owns the serialization and introspection shared libraries and exposes a
/// runtime serializer plus introspection members. Shared by the runtime
/// service client and the generic service server.
struct MessageTypeSupport {
  /// @brief Introspection type-support namespace alias.
  using MessageMembers = rosidl_typesupport_introspection_cpp::MessageMembers;

  /// @brief Load serialization and introspection type support for @p type.
  explicit MessageTypeSupport(const std::string &type);

  /// @brief Shared library that owns the serialization handle.
  std::shared_ptr<rcpputils::SharedLibrary> serialization_library;
  /// @brief Shared library that owns the introspection handle.
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  /// @brief C++ serialization type-support handle.
  const rosidl_message_type_support_t *serialization_handle;
  /// @brief C++ introspection type-support handle.
  const rosidl_message_type_support_t *introspection_handle;
  /// @brief Message member metadata from introspection type support.
  const MessageMembers &members;
  /// @brief Runtime serializer for this message type.
  rclcpp::SerializationBase serializer;

private:
  /// @brief Require message introspection data.
  static const MessageMembers &requireMembers(const rosidl_message_type_support_t *handle);
};

/// @brief Runtime type-support data for one ROS service type.
///
/// Bundles the service handle (used for rcl client/service init) with request
/// and response message type support.
struct ServiceTypeSupport {
  /// @brief Load service, request, and response type support for @p type.
  /// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
  /// @param error Populated with a failure reason when creation fails.
  /// @return Loaded type support, or nullptr when service type support
  ///   cannot be resolved.
  static std::shared_ptr<ServiceTypeSupport> create(const std::string &type, std::string &error);

  /// @brief Shared library that owns the service handle.
  std::shared_ptr<rcpputils::SharedLibrary> library;
  /// @brief C++ service type-support handle.
  const rosidl_service_type_support_t *handle;
  /// @brief Request message type support.
  MessageTypeSupport request;
  /// @brief Response message type support.
  MessageTypeSupport response;

private:
  ServiceTypeSupport(const std::string &type, std::shared_ptr<rcpputils::SharedLibrary> library,
                     const rosidl_service_type_support_t *handle);

  /// @brief Request message type-name suffix.
  static constexpr char kRequestMessageTypeSuffix[] = "_Request";
  /// @brief Response message type-name suffix.
  static constexpr char kResponseMessageTypeSuffix[] = "_Response";
};

} // namespace ros2_livekit_bridge::ros2_cli
