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

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>

#include "diagnostics_test_utils.hpp"
#include "ros_portal/diagnostics/build_info.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"

namespace ros_portal::diagnostics {
namespace {

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

TEST(BuildInfoDiagnosticsTest, PopulatesOkStatusWithVersionFields) {
  BuildInfo info;
  info.livekit_sdk_version = "1.9.0";
  info.ros_portal_version = "0.1.0";
  info.ros_distro = "humble";
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateBuildInfoStatus(info, status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(status.message, "LiveKit SDK 1.9.0");
  EXPECT_EQ(valueFor(status, "livekit_sdk_version"), "1.9.0");
  EXPECT_EQ(valueFor(status, "ros_portal_version"), "0.1.0");
  EXPECT_EQ(valueFor(status, "ros_distro"), "humble");
}

TEST(BuildInfoDiagnosticsTest, CollectedInfoHasNoEmptyFields) {
  const auto info = collectBuildInfo();

  EXPECT_FALSE(info.livekit_sdk_version.empty());
  EXPECT_FALSE(info.ros_portal_version.empty());
  EXPECT_FALSE(info.ros_distro.empty());
}

TEST(BuildInfoDiagnosticsTest, FormatsOtherSdksAsPortalDistroVersion) {
  BuildInfo info;
  info.livekit_sdk_version = "1.9.0";
  info.ros_portal_version = "0.1.0";
  info.ros_distro = "jazzy";

  EXPECT_EQ(formatOtherSdks(info), "ros-portal:jazzy-v0.1.0");
}

TEST(BuildInfoDiagnosticsTest, FormatsOtherSdksWithUnknownDistro) {
  BuildInfo info;
  info.ros_portal_version = "0.1.0";
  info.ros_distro = "unknown";

  EXPECT_EQ(formatOtherSdks(info), "ros-portal:unknown-v0.1.0");
}

TEST(BuildInfoDiagnosticsTest, FormatsOtherSdksFromCollectedInfo) {
  // The value handed to RoomOptions::other_sdks must always attribute the
  // participant to ROS Portal as `ros-portal:<ros_distro>-<version>` with no
  // whitespace.
  const auto info = collectBuildInfo();
  const std::string other_sdks = formatOtherSdks(info);

  EXPECT_EQ(other_sdks, "ros-portal:" + info.ros_distro + "-v" + info.ros_portal_version) << "actual: " << other_sdks;
  EXPECT_EQ(other_sdks.find_first_of(" \t"), std::string::npos) << "actual: " << other_sdks;
}

TEST(BuildInfoDiagnosticsTest, RegistersAndRemovesTaskWithSharedHub) {
  auto node = std::make_shared<rclcpp::Node>("build_info_diagnostics_unit_test");
  const auto diagnostics_updater = std::make_shared<diagnostic_updater::Updater>(node);
  diagnostics_updater->setHardwareID("ros_portal");
  const auto fns = test::makeDiagnosticsFns(diagnostics_updater);

  {
    BuildInfoDiagnostics build_info(fns);
  }
}

TEST(BuildInfoDiagnosticsTest, RejectsMissingDiagnostics) {
  EXPECT_THROW(BuildInfoDiagnostics({}), std::invalid_argument);
}

} // namespace
} // namespace ros_portal::diagnostics
