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

#include <gtest/gtest.h>

#include <vector>

namespace ros2_livekit_bridge {
namespace {

ServiceListOptions makeServiceOptions(bool show_types = false, bool count_services = false,
                                      bool include_hidden_services = false) {
  ServiceListOptions options;
  options.show_types = show_types;
  options.count_services = count_services;
  options.include_hidden_services = include_hidden_services;
  return options;
}

TEST(Ros2ServiceListTest, ListsServiceNamesOnePerLine) {
  const std::vector<Ros2CliManager::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty"}},
  };

  EXPECT_EQ(ros2_cli::formatServiceList(services, makeServiceOptions()), "/alpha\n/beta\n");
}

TEST(Ros2ServiceListTest, ShowTypesListsServiceTypes) {
  const std::vector<Ros2CliManager::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty", "custom_msgs/srv/Thing"}},
  };

  EXPECT_EQ(ros2_cli::formatServiceList(services, makeServiceOptions(true)),
            "/alpha [example_interfaces/srv/Trigger]\n"
            "/beta [std_srvs/srv/Empty, custom_msgs/srv/Thing]\n");
}

TEST(Ros2ServiceListTest, CountServicesOnlyPrintsServiceCount) {
  const std::vector<Ros2CliManager::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty"}},
  };

  EXPECT_EQ(ros2_cli::formatServiceList(services, makeServiceOptions(true, true)), "2\n");
}

TEST(Ros2ServiceListTest, EmptyServiceListProducesEmptyOutput) {
  EXPECT_EQ(ros2_cli::formatServiceList({}, makeServiceOptions()), "");
}

TEST(Ros2ServiceListTest, DetectsHiddenServiceTokens) {
  EXPECT_FALSE(ros2_cli::isHiddenService("/visible/service"));
  EXPECT_TRUE(ros2_cli::isHiddenService("/_hidden/service"));
  EXPECT_TRUE(ros2_cli::isHiddenService("/visible/_hidden"));
}

} // namespace
} // namespace ros2_livekit_bridge
