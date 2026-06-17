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

#include "ros2_livekit_bridge/ros2_cli/ros2_service_list.hpp"
#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace ros2_livekit_bridge::ros2_cli
{

/// @copydoc isHiddenService()
bool isHiddenService(const std::string & service_name)
{
  return hasHiddenNameToken(service_name);
}

/// @copydoc formatServiceList()
std::string
formatServiceList(
  const std::vector<Ros2CliManager::ServiceInfo> & services,
  const ServiceListOptions & options)
{
  std::ostringstream stream;

  if (options.count_services) {
    stream << services.size() << '\n';
    return stream.str();
  }

  for (const auto & service : services) {
    stream << service.name;
    if (options.show_types) {
      stream << " [" << joinTypes(service.types) << "]";
    }
    stream << '\n';
  }

  return stream.str();
}

/// @copydoc collectServiceInfo()
std::vector<Ros2CliManager::ServiceInfo>
collectServiceInfo(
  const rclcpp::node_interfaces::NodeGraphInterface & graph,
  const ServiceListOptions & options)
{
  std::vector<Ros2CliManager::ServiceInfo> services;
  const auto service_names_and_types = graph.get_service_names_and_types();
  services.reserve(service_names_and_types.size());

  for (const auto &[service_name, service_types] : service_names_and_types) {
    if (!options.include_hidden_services && isHiddenService(service_name)) {
      continue;
    }

    Ros2CliManager::ServiceInfo service_info;
    service_info.name = service_name;
    service_info.types = service_types;
    services.push_back(std::move(service_info));
  }

  std::sort(services.begin(), services.end(),
    [](const Ros2CliManager::ServiceInfo & lhs,
    const Ros2CliManager::ServiceInfo & rhs) {
      return lhs.name < rhs.name;
            });
  return services;
}

} // namespace ros2_livekit_bridge::ros2_cli
