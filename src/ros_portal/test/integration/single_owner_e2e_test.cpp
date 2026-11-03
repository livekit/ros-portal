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

#include <gtest/gtest.h>
#include <livekit/livekit.h>

#include <chrono>
#include <cstdlib>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "ros_portal/ros_portal.hpp"
#include "ros_portal/utils/ros_utils.hpp"
#include "test_common.hpp"

namespace ros_portal::test {
namespace {

TEST(SingleOwnerE2E, ConnectsOnSingleThreadAndReleasesOwnedRuntime) {
  const auto token = utils::environmentVariable("LIVEKIT_TOKEN_A");
  ASSERT_TRUE(token.has_value()) << "Source scripts/set-test-tokens.sh before running integration tests";
  ASSERT_TRUE(utils::environmentVariable("LIVEKIT_URL").has_value());
  const ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  const ScopedEnvVar scoped_endpoint{"LIVEKIT_TOKEN_ENDPOINT"};
  const ScopedEnvVar scoped_server_id{"LIVEKIT_TOKEN_SERVER_ID"};
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", *token));
  unsetenv("LIVEKIT_TOKEN_ENDPOINT");
  unsetenv("LIVEKIT_TOKEN_SERVER_ID");

  // This test exercises node ownership, unlike paired-portal fixtures whose
  // harness initializes and retains the shared test runtime externally.
  (void)livekit::initialize();
  livekit::shutdown();
  const auto domains = testDomainIds();
  ScopedRosGraph graph(domains.first);
  auto portal = std::make_unique<RosPortal>(rclcpp::NodeOptions().context(graph.context()));
  EXPECT_TRUE(portal->weak_from_this().expired());
  auto observer =
      std::make_unique<rclcpp::Node>("single_owner_observer", rclcpp::NodeOptions().context(graph.context()));
  bool connected = false;
  auto diagnostics = observer->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
      "/diagnostics", 10, [&connected](const diagnostic_msgs::msg::DiagnosticArray::ConstSharedPtr& message) {
        for (const auto& status : message->status) {
          if (status.name.find("connection_health") != std::string::npos &&
              status.level == diagnostic_msgs::msg::DiagnosticStatus::OK) {
            connected = true;
          }
        }
      });
  rclcpp::ExecutorOptions executor_options;
  executor_options.context = graph.context();
  rclcpp::executors::SingleThreadedExecutor executor(executor_options);
  executor.add_node(portal->get_node_base_interface());
  executor.add_node(observer->get_node_base_interface());
  EXPECT_TRUE(waitFor(
      [&]() {
        executor.spin_some();
        return connected;
      },
      std::chrono::seconds(15)))
      << "The SDK-owning node did not connect on its single-threaded executor";
  executor.remove_node(portal->get_node_base_interface());
  executor.remove_node(observer->get_node_base_interface());
  portal.reset();

  EXPECT_TRUE(livekit::initialize()) << "Node destruction did not release its owned SDK";
  livekit::shutdown();
}

} // namespace
} // namespace ros_portal::test
