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

#include "ros_portal/diagnostics/build_info.hpp"

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <stdexcept>
#include <utility>

#include "ros_portal/utils/ros_utils.hpp"
#include "ros_portal/version.hpp"

namespace ros_portal::diagnostics {

namespace {

constexpr char kBuildInfoTaskName[] = "build_info";
constexpr char kUnknownValue[] = "unknown";

std::string valueOrUnknown(const char* value) { return (value != nullptr && value[0] != '\0') ? value : kUnknownValue; }

} // namespace

BuildInfo collectBuildInfo() {
  BuildInfo info;
  info.livekit_sdk_version = valueOrUnknown(ROS_PORTAL_LIVEKIT_SDK_VERSION);
  info.ros_portal_version = valueOrUnknown(ROS_PORTAL_VERSION);
  info.ros_distro = utils::environmentVariable("ROS_DISTRO").value_or(kUnknownValue);
  return info;
}

void populateBuildInfoStatus(const BuildInfo& info, diagnostic_updater::DiagnosticStatusWrapper& status) {
  status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "LiveKit SDK " + info.livekit_sdk_version);
  status.add("livekit_sdk_version", info.livekit_sdk_version);
  status.add("ros_portal_version", info.ros_portal_version);
  status.add("ros_distro", info.ros_distro);
}

std::string formatOtherSdks(const BuildInfo& info) {
  return "ros-portal:" + info.ros_distro + "-v" + info.ros_portal_version;
}

BuildInfoDiagnostics::BuildInfoDiagnostics(DiagnosticsManagerFns diagnostics)
    : info_(collectBuildInfo()), diagnostics_(std::move(diagnostics)) {
  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("BuildInfoDiagnostics requires fully populated DiagnosticsManagerFns");
  }

  diagnostics_.add(kBuildInfoTaskName, [this](diagnostic_updater::DiagnosticStatusWrapper& status) {
    populateBuildInfoStatus(info_, status);
  });
}

BuildInfoDiagnostics::~BuildInfoDiagnostics() { diagnostics_.remove(kBuildInfoTaskName); }

} // namespace ros_portal::diagnostics
