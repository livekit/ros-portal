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

#include "ros2_livekit_bridge/cli/service_list.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ros2_livekit_bridge/cli/utils.hpp"

namespace ros2_livekit_bridge::cli {

bool isHiddenService(const std::string& service_name) { return hasHiddenNameToken(service_name); }

std::string formatServiceList(const std::vector<ServiceInfo>& services, const ServiceListOptions& options) {
  std::ostringstream stream;

  if (options.count_services) {
    stream << services.size() << '\n';
    return stream.str();
  }

  for (const auto& service : services) {
    stream << service.name;
    if (options.show_types) {
      stream << " [" << joinTypes(service.types) << "]";
    }
    stream << '\n';
  }

  return stream.str();
}

std::vector<ServiceInfo> collectServiceInfo(const rclcpp::node_interfaces::NodeGraphInterface& graph,
                                            const ServiceListOptions& options) {
  std::vector<ServiceInfo> services;
  const auto service_names_and_types = graph.get_service_names_and_types();
  services.reserve(service_names_and_types.size());

  for (const auto& [service_name, service_types] : service_names_and_types) {
    if (!options.include_hidden_services && isHiddenService(service_name)) {
      continue;
    }

    ServiceInfo service_info;
    service_info.name = service_name;
    service_info.types = service_types;
    services.push_back(std::move(service_info));
  }

  std::sort(services.begin(), services.end(),
            [](const ServiceInfo& lhs, const ServiceInfo& rhs) { return lhs.name < rhs.name; });
  return services;
}

} // namespace ros2_livekit_bridge::cli
