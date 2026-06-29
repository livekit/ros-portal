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

/// @file Compatibility shims for rclcpp API differences across ROS 2 distros
/// (Humble, Jazzy, Kilted, Lyrical).

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/typesupport_helpers.hpp>

#include <memory>
#include <string>

// Humble ships rclcpp 16.x; Jazzy ships 28.x; Kilted 29.x+.
// Use RCLCPP_VERSION_MAJOR to select the appropriate API surface.
#ifndef RCLCPP_VERSION_MAJOR
#include <rclcpp/version.h>
#endif

namespace ros2_livekit_bridge::compat
{

/// Retrieve a message type-support handle from a loaded shared library.
/// Jazzy+ renamed `get_typesupport_handle` → `get_message_typesupport_handle`.
inline const rosidl_message_type_support_t * get_message_typesupport_handle(
  const std::string & type,
  const std::string & typesupport_identifier,
  rcpputils::SharedLibrary & library)
{
#if RCLCPP_VERSION_MAJOR >= 28
  return rclcpp::get_message_typesupport_handle(type, typesupport_identifier, library);
#else
  return rclcpp::get_typesupport_handle(type, typesupport_identifier, library);
#endif
}

/// Create a ROS 2 service with default QoS, compatible across distros.
/// Humble's `create_service` takes `const rmw_qos_profile_t &`; Jazzy+ accepts
/// `rclcpp::QoS` or `rclcpp::ServicesQoS`.
template<typename ServiceT, typename CallbackT>
typename rclcpp::Service<ServiceT>::SharedPtr create_service(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base,
  rclcpp::node_interfaces::NodeServicesInterface::SharedPtr node_services,
  const std::string & service_name,
  CallbackT && callback,
  rclcpp::CallbackGroup::SharedPtr group)
{
#if RCLCPP_VERSION_MAJOR >= 28
  return rclcpp::create_service<ServiceT>(
    node_base, node_services, service_name,
    std::forward<CallbackT>(callback),
    rclcpp::ServicesQoS(), group);
#else
  return rclcpp::create_service<ServiceT>(
    node_base, node_services, service_name,
    std::forward<CallbackT>(callback),
    rmw_qos_profile_services_default, group);
#endif
}

}  // namespace ros2_livekit_bridge::compat
