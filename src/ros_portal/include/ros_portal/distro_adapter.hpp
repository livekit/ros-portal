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

#include <rclcpp/qos.hpp>

namespace ros_portal {

/// Default QoS for ROS services, in the form the distro's `create_service` accepts.
/// Humble takes an `rmw_qos_profile_t`. Newer distros take `rclcpp::ServicesQoS`.
inline auto servicesQos() {
#ifdef ROS_DISTRO_HUMBLE
  return rmw_qos_profile_services_default;
#else
  return rclcpp::ServicesQoS();
#endif
}

} // namespace ros_portal
