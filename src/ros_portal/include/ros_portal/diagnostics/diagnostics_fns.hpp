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

#include <diagnostic_updater/diagnostic_updater.hpp>
#include <functional>
#include <string>

namespace ros_portal::diagnostics {

/// Diagnostics registration functions handed to ROS Portal components.
///
/// ROS Portal node owns the shared `diagnostic_updater::Updater` and passes
/// components this bundle of wrapper functions.
struct DiagnosticsManagerFns {
  /// Task callback that populates one diagnostic status.
  using TaskCallback = std::function<void(diagnostic_updater::DiagnosticStatusWrapper&)>;

  /// Register a diagnostic task callback under a stable name.
  std::function<void(const std::string& name, TaskCallback callback)> add;

  /// Deregister a diagnostic task by name.
  std::function<void(const std::string& name)> remove;
};

} // namespace ros_portal::diagnostics
