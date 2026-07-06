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

#include "ros2_livekit_bridge/introspection/runtime_type_support.hpp"

#include <exception>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <stdexcept>
#include <utility>

#include "ros2_livekit_bridge/introspection/introspection_utils.hpp"

namespace ros2_livekit_bridge::introspection {

namespace {
constexpr char kServiceTypeSupportSymbolPrefix[] = "__get_service_type_support_handle__";
constexpr char kRequestMessageTypeSuffix[] = "_Request";
constexpr char kResponseMessageTypeSuffix[] = "_Response";
} // namespace

std::string serviceTypeSupportSymbol(const std::string& type, const std::string& typesupport_identifier) {
  std::string symbol = typesupport_identifier + kServiceTypeSupportSymbolPrefix;
  for (const char ch : type) {
    if (ch == '/') {
      symbol += "__";
    } else {
      symbol += ch;
    }
  }
  return symbol;
}

const rosidl_service_type_support_t* serviceTypeSupportHandle(const std::string& type,
                                                              const std::string& typesupport_identifier,
                                                              rcpputils::SharedLibrary& library) {
  const std::string symbol = serviceTypeSupportSymbol(type, typesupport_identifier);
  if (!library.has_symbol(symbol)) {
    return nullptr;
  }
  using GetServiceTypeSupportHandleFn = const rosidl_service_type_support_t* (*)();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto get_handle = reinterpret_cast<GetServiceTypeSupportHandleFn>(library.get_symbol(symbol));
  return get_handle();
}

RuntimeMessageTypeSupport::RuntimeMessageTypeSupport(const std::string& type)
    : serialization_library(rclcpp::get_typesupport_library(type, rosidl_typesupport_cpp::typesupport_identifier)),
      introspection_library(
          rclcpp::get_typesupport_library(type, rosidl_typesupport_introspection_cpp::typesupport_identifier)),
      serialization_handle(rclcpp::get_message_typesupport_handle(type, rosidl_typesupport_cpp::typesupport_identifier,
                                                                  *serialization_library)),
      introspection_handle(rclcpp::get_message_typesupport_handle(
          type, rosidl_typesupport_introspection_cpp::typesupport_identifier, *introspection_library)),
      members(requireMembers(introspection_handle)),
      serializer(serialization_handle) {}

const rosidl_typesupport_introspection_cpp::MessageMembers& RuntimeMessageTypeSupport::requireMembers(
    const rosidl_message_type_support_t* handle) {
  const auto* members = membersFromHandle(handle);
  if (members == nullptr) {
    throw std::runtime_error("Introspection type support handle is null");
  }
  return *members;
}

RuntimeServiceTypeSupport::RuntimeServiceTypeSupport(const std::string& type,
                                                     std::shared_ptr<rcpputils::SharedLibrary> library,
                                                     const rosidl_service_type_support_t* handle)
    : library(std::move(library)),
      handle(handle),
      request(type + kRequestMessageTypeSuffix),
      response(type + kResponseMessageTypeSuffix) {}

std::shared_ptr<RuntimeServiceTypeSupport> RuntimeServiceTypeSupport::create(const std::string& type,
                                                                             std::string& error) {
  try {
    auto library = rclcpp::get_typesupport_library(type, rosidl_typesupport_cpp::typesupport_identifier);
    const auto* handle = serviceTypeSupportHandle(type, rosidl_typesupport_cpp::typesupport_identifier, *library);
    if (handle == nullptr) {
      error = "Service typesupport symbol not found: " +
              serviceTypeSupportSymbol(type, rosidl_typesupport_cpp::typesupport_identifier);
      return nullptr;
    }
    return std::shared_ptr<RuntimeServiceTypeSupport>(new RuntimeServiceTypeSupport(type, std::move(library), handle));
  } catch (const std::exception& exception) {
    error = exception.what();
    return nullptr;
  }
}

} // namespace ros2_livekit_bridge::introspection
