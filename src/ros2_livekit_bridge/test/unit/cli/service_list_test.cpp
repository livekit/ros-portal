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

TEST(ServiceListTest, ListsServiceNamesOnePerLine) {
  const std::vector<cli::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty"}},
  };

  EXPECT_EQ(cli::formatServiceList(services, makeServiceOptions()), "/alpha\n/beta\n");
}

TEST(ServiceListTest, ShowTypesListsServiceTypes) {
  const std::vector<cli::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty", "custom_msgs/srv/Thing"}},
  };

  EXPECT_EQ(cli::formatServiceList(services, makeServiceOptions(true)),
            "/alpha [example_interfaces/srv/Trigger]\n"
            "/beta [std_srvs/srv/Empty, custom_msgs/srv/Thing]\n");
}

TEST(ServiceListTest, CountServicesOnlyPrintsServiceCount) {
  const std::vector<cli::ServiceInfo> services{
      {"/alpha", {"example_interfaces/srv/Trigger"}},
      {"/beta", {"std_srvs/srv/Empty"}},
  };

  EXPECT_EQ(cli::formatServiceList(services, makeServiceOptions(true, true)), "2\n");
}

TEST(ServiceListTest, EmptyServiceListProducesEmptyOutput) {
  EXPECT_EQ(cli::formatServiceList({}, makeServiceOptions()), "");
}

TEST(ServiceListTest, DetectsHiddenServiceTokens) {
  EXPECT_FALSE(cli::isHiddenService("/visible/service"));
  EXPECT_TRUE(cli::isHiddenService("/_hidden/service"));
  EXPECT_TRUE(cli::isHiddenService("/visible/_hidden"));
}

} // namespace
} // namespace ros2_livekit_bridge
