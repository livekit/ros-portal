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

#include <cstdlib>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <stdexcept>
#include <utility>

// Injected by CMake from the resolved LiveKit package version and package.xml;
// guarded so tooling that compiles this file without CMake still parses it.
#ifndef ROS_PORTAL_LIVEKIT_SDK_VERSION
#define ROS_PORTAL_LIVEKIT_SDK_VERSION ""
#endif
#ifndef ROS_PORTAL_PACKAGE_VERSION
#define ROS_PORTAL_PACKAGE_VERSION ""
#endif

namespace ros_portal::diagnostics {

namespace {

constexpr char kBuildInfoTaskName[] = "build_info";
constexpr char kUnknownValue[] = "unknown";

std::string valueOrUnknown(const char* value) { return (value != nullptr && value[0] != '\0') ? value : kUnknownValue; }

} // namespace

BuildInfo collectBuildInfo() {
  BuildInfo info;
  info.livekit_sdk_version = valueOrUnknown(ROS_PORTAL_LIVEKIT_SDK_VERSION);
  info.ros_portal_version = valueOrUnknown(ROS_PORTAL_PACKAGE_VERSION);
  info.ros_distro = valueOrUnknown(std::getenv("ROS_DISTRO"));
  return info;
}

void populateBuildInfoStatus(const BuildInfo& info, diagnostic_updater::DiagnosticStatusWrapper& status) {
  status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "LiveKit SDK " + info.livekit_sdk_version);
  status.add("livekit_sdk_version", info.livekit_sdk_version);
  status.add("ros_portal_version", info.ros_portal_version);
  status.add("ros_distro", info.ros_distro);
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
