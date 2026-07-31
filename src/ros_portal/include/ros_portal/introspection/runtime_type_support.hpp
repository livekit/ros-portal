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

namespace ros_portal::introspection {

/// @brief Build the service type-support symbol name for @p type.
/// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
/// @param typesupport_identifier Type-support identifier to build the symbol.
/// @return Symbol name to resolve in the type-support shared library.
std::string serviceTypeSupportSymbol(const std::string& type, const std::string& typesupport_identifier);

/// @brief Load service type support by symbol from a type-support library.
/// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
/// @param typesupport_identifier Type-support identifier to build the symbol.
/// @param library Loaded type-support shared library to resolve the symbol in.
/// @return Service type-support handle, or nullptr when the symbol is missing.
const rosidl_service_type_support_t* serviceTypeSupportHandle(const std::string& type,
                                                              const std::string& typesupport_identifier,
                                                              rcpputils::SharedLibrary& library);

/// @brief Runtime type-support data for one ROS message type.
struct RuntimeMessageTypeSupport {
  /// @brief Load serialization and introspection type support for @p type.
  /// @param type ROS message type identifier.
  explicit RuntimeMessageTypeSupport(const std::string& type);

  /// @brief Shared library that owns the serialization handle.
  std::shared_ptr<rcpputils::SharedLibrary> serialization_library;
  /// @brief Shared library that owns the introspection handle.
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  /// @brief C++ serialization type-support handle.
  const rosidl_message_type_support_t* serialization_handle;
  /// @brief C++ introspection type-support handle.
  const rosidl_message_type_support_t* introspection_handle;
  /// @brief Message member metadata from introspection type support.
  const rosidl_typesupport_introspection_cpp::MessageMembers& members;
  /// @brief Runtime serializer for this message type.
  rclcpp::SerializationBase serializer;

private:
  /// @brief Require message introspection data.
  static const rosidl_typesupport_introspection_cpp::MessageMembers& requireMembers(
      const rosidl_message_type_support_t* handle);
};

/// @brief Runtime type-support data for one ROS service type.
struct RuntimeServiceTypeSupport {
  /// @brief Load service, request, and response type support for @p type.
  /// @param type Service type identifier, such as `std_srvs/srv/SetBool`.
  /// @param error Populated with a failure reason when creation fails.
  /// @return Loaded type support, or nullptr when service type support cannot
  /// be resolved.
  static std::shared_ptr<RuntimeServiceTypeSupport> create(const std::string& type, std::string& error);

  /// @brief Shared library that owns the service handle.
  std::shared_ptr<rcpputils::SharedLibrary> library;
  /// @brief C++ service type-support handle.
  const rosidl_service_type_support_t* handle;
  /// @brief Request message type support.
  RuntimeMessageTypeSupport request;
  /// @brief Response message type support.
  RuntimeMessageTypeSupport response;

private:
  RuntimeServiceTypeSupport(const std::string& type, std::shared_ptr<rcpputils::SharedLibrary> library,
                            const rosidl_service_type_support_t* handle);
};

} // namespace ros_portal::introspection
