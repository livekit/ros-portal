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

#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <string>

#include "ros_portal/diagnostics/diagnostics_fns.hpp"

namespace ros_portal::diagnostics {

/// Static build and dependency version facts reported by the `build_info` task.
struct BuildInfo {
  /// LiveKit C++ SDK version ROS Portal was built against.
  std::string livekit_sdk_version;

  /// ROS Portal package version from package.xml.
  std::string ros_portal_version;

  /// ROS distribution name from the `ROS_DISTRO` environment variable.
  std::string ros_distro;
};

/// Collect build info baked in at compile time plus runtime environment facts.
///
/// Fields that cannot be determined are reported as `unknown` rather than
/// omitted, so the diagnostic surface stays fixed.
BuildInfo collectBuildInfo();

/// Populate a ROS diagnostic status from build info.
///
/// This pure mapping function is shared by the runtime helper and unit tests.
/// The status level is always `OK`; the summary message carries the LiveKit
/// SDK version so it is visible without expanding key/value fields.
///
/// @param info Build info to render.
/// @param status Diagnostic status wrapper to populate.
void populateBuildInfoStatus(const BuildInfo& info, diagnostic_updater::DiagnosticStatusWrapper& status);

/// Render build info as the LiveKit client info `other_sdks` field.
/// This pure mapping function is shared by the connect path and unit tests.
///
/// @param info Build info to render.
/// @return `ros_portal:<ros_distro>-<version>`
std::string formatOtherSdks(const BuildInfo& info);

/// Maintains the always-OK `build_info` diagnostic task.
///
/// The reported values are immutable for the lifetime of the process, so this
/// helper collects them once at construction and re-emits them on every
/// updater cycle.
class BuildInfoDiagnostics final {
public:
  /// Register the build info task through ROS Portal-owned diagnostics functions.
  ///
  /// @param diagnostics ROS Portal-owned diagnostics functions wrapping the shared
  /// manager.
  /// @throws std::invalid_argument when @p diagnostics is incomplete.
  explicit BuildInfoDiagnostics(DiagnosticsManagerFns diagnostics);

  /// Remove the registered build info task from the shared manager.
  ///
  /// The diagnostics manager must outlive this helper because its timer owns the
  /// registered callback until this destructor deregisters it.
  ~BuildInfoDiagnostics();

  BuildInfoDiagnostics(const BuildInfoDiagnostics&) = delete;
  BuildInfoDiagnostics& operator=(const BuildInfoDiagnostics&) = delete;

private:
  /// Build info collected once at construction and rendered on every cycle.
  BuildInfo info_;

  /// ROS Portal-owned diagnostics functions used to (de)register the `build_info` task.
  DiagnosticsManagerFns diagnostics_;
};

} // namespace ros_portal::diagnostics
