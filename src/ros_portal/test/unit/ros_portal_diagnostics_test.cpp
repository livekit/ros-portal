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

#include <chrono>
#include <cstdlib>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <filesystem>
#include <fstream>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "ros_portal/ros_portal.hpp"
#include "test_common.hpp"

namespace ros_portal {

namespace {

using ros_portal::test::ScopedEnvVar;

std::optional<std::string> diagnosticValueFor(const diagnostic_updater::DiagnosticStatusWrapper& status,
                                              const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

class TemporaryDiagnosticsConfig {
public:
  explicit TemporaryDiagnosticsConfig(const char* topics_yaml = "  topics: []\n")
      : path_(std::filesystem::temp_directory_path() /
              ("ros_portal_diagnostics_test_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml")) {
    std::ofstream output(path_);
    output << "ros_portal:\n"
              "  version: \"0.0.1\"\n"
              "  ros_threads: 3\n"
           << topics_yaml;
  }

  ~TemporaryDiagnosticsConfig() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

TEST(RosPortalDiagnosticsTest, ReportsPartialInitializationAndEffectiveConfiguration) {
  ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  unsetenv("LIVEKIT_URL");
  unsetenv("LIVEKIT_TOKEN");
  TemporaryDiagnosticsConfig config;

  const rclcpp::NodeOptions options = rclcpp::NodeOptions().parameter_overrides({
      rclcpp::Parameter("config_path", config.path().string()),
      rclcpp::Parameter("min_qos_depth", 2),
      rclcpp::Parameter("max_qos_depth", 8),
  });
  RosPortal portal(options);

  EXPECT_FALSE(portal.initialize());

  diagnostic_updater::DiagnosticStatusWrapper status;
  portal.populateStatus(status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(status.message, "ROS Portal is not initialized");
  EXPECT_EQ(diagnosticValueFor(status, "initialized"), "false");
  EXPECT_EQ(diagnosticValueFor(status, "components_inactive"),
            "connection_manager,topic_forwarder,service_forwarder,cli_manager");
  EXPECT_EQ(diagnosticValueFor(status, "config_path"), config.path().string());
  EXPECT_EQ(diagnosticValueFor(status, "graph_discovery_active"), "false");
  EXPECT_EQ(diagnosticValueFor(status, "local_identity"), "unset");
  EXPECT_FALSE(diagnosticValueFor(status, "min_qos_depth").has_value());
  EXPECT_FALSE(diagnosticValueFor(status, "max_qos_depth").has_value());
  EXPECT_FALSE(diagnosticValueFor(status, "ros_threads").has_value());
}

TEST(RosPortalDiagnosticsTest, ReportsHealthyAndInactiveDiscoveryStates) {
  RosPortal portal;
  portal.initialized_.store(true);
  portal.diagnostic_state_.graph_discovery_active.store(true);
  portal.diagnostic_state_.connection_manager_active.store(true);
  portal.diagnostic_state_.topic_forwarder_active.store(true);
  portal.diagnostic_state_.service_forwarder_active.store(true);
  portal.diagnostic_state_.cli_manager_active.store(true);

  diagnostic_updater::DiagnosticStatusWrapper healthy_status;
  portal.populateStatus(healthy_status);

  EXPECT_EQ(healthy_status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(healthy_status.message, "ROS Portal is initialized");
  EXPECT_EQ(diagnosticValueFor(healthy_status, "components_inactive"), "none");

  portal.diagnostic_state_.graph_discovery_active.store(false);
  diagnostic_updater::DiagnosticStatusWrapper stalled_status;
  portal.populateStatus(stalled_status);
  EXPECT_EQ(stalled_status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(stalled_status.message, "ROS Portal is initialized without an active graph discovery worker");
}

TEST(RosPortalDiagnosticsTest, OmitsLatchedForwarderWhenNoLatchedTopicsConfigured) {
  RosPortal portal;
  portal.initialized_.store(true);
  portal.diagnostic_state_.graph_discovery_active.store(true);
  portal.diagnostic_state_.connection_manager_active.store(true);
  portal.diagnostic_state_.topic_forwarder_active.store(true);
  portal.diagnostic_state_.service_forwarder_active.store(true);
  portal.diagnostic_state_.cli_manager_active.store(true);

  diagnostic_updater::DiagnosticStatusWrapper status;
  portal.populateStatus(status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(status.message, "ROS Portal is initialized");
  EXPECT_EQ(diagnosticValueFor(status, "components_inactive"), "none");
}

TEST(RosPortalDiagnosticsTest, ReportsInactiveLatchedForwarderWhenLatchedTopicsConfigured) {
  ScopedEnvVar scoped_url{"LIVEKIT_URL"};
  ScopedEnvVar scoped_token{"LIVEKIT_TOKEN"};
  unsetenv("LIVEKIT_URL");
  unsetenv("LIVEKIT_TOKEN");
  TemporaryDiagnosticsConfig config(R"(  topics:
    - topic: "/tf_static"
      direction: "out"
      latched: true
)");

  const rclcpp::NodeOptions options = rclcpp::NodeOptions().parameter_overrides({
      rclcpp::Parameter("config_path", config.path().string()),
  });
  RosPortal portal(options);

  EXPECT_FALSE(portal.initialize());

  diagnostic_updater::DiagnosticStatusWrapper status;
  portal.populateStatus(status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(status.message, "ROS Portal is not initialized");
  EXPECT_EQ(diagnosticValueFor(status, "components_inactive"),
            "connection_manager,topic_forwarder,latched_topic_forwarder,service_forwarder,cli_manager");

  portal.initialized_.store(true);
  portal.diagnostic_state_.graph_discovery_active.store(true);
  portal.diagnostic_state_.connection_manager_active.store(true);
  portal.diagnostic_state_.topic_forwarder_active.store(true);
  portal.diagnostic_state_.service_forwarder_active.store(true);
  portal.diagnostic_state_.cli_manager_active.store(true);
  ASSERT_TRUE(portal.diagnostic_state_.latched_topic_forwarder_active.has_value());
  portal.diagnostic_state_.latched_topic_forwarder_active->store(false, std::memory_order_relaxed);

  diagnostic_updater::DiagnosticStatusWrapper inactive_status;
  portal.populateStatus(inactive_status);
  EXPECT_EQ(inactive_status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(inactive_status.message, "ROS Portal has inactive components");
  EXPECT_EQ(diagnosticValueFor(inactive_status, "components_inactive"), "latched_topic_forwarder");
}

} // namespace ros_portal
