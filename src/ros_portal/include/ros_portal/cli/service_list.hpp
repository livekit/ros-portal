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

#include <string>
#include <vector>

#include "ros_portal/cli/types.hpp"
#include "ros_portal/graph_types.hpp"

namespace ros_portal::cli {

/// @brief Check whether a service name should be treated as hidden.
/// @param service_name Fully qualified or relative ROS service name.
/// @return True when any service name token starts with an underscore.
bool isHiddenService(const std::string& service_name);

/// @brief Render discovered service information in `ros2 service list` format.
/// @param services Service metadata to render. The caller owns filtering and
/// sort order.
/// @param options Formatting options that control count and type output.
/// @return Human-readable command output ending in a newline when non-empty.
std::string formatServiceList(const std::vector<ServiceInfo>& services, const ServiceListOptions& options);

/// @brief Collect visible service metadata from a shared names/types snapshot.
/// @param services Cached service names and types.
/// @param options Discovery options, including hidden-service filtering.
/// @return Service metadata sorted by service name.
std::vector<ServiceInfo> collectServiceInfo(const ServiceNamesAndTypes& services, const ServiceListOptions& options);

} // namespace ros_portal::cli
