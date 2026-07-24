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
#include <utility>

namespace ros2_livekit_bridge::diagnostics {

/// Diagnostics registration functions handed to bridge components.
///
/// The bridge owns the `DiagnosticsManager` and passes components this bundle
/// of wrapper functions instead of a raw manager pointer. The bridge-owned
/// wrappers validate the manager before forwarding, so components never touch
/// a dangling pointer. A default-constructed bundle (empty functions) means
/// diagnostics are disabled; components must check each function before
/// calling it.
struct DiagnosticsManagerFns {
  /// Task callback that populates one diagnostic status.
  using TaskCallback = std::function<void(diagnostic_updater::DiagnosticStatusWrapper&)>;

  /// Register a diagnostic task callback under a stable name.
  std::function<void(const std::string& name, TaskCallback callback)> add;

  /// Deregister a diagnostic task by name.
  std::function<void(const std::string& name)> remove;
};

/// Shared diagnostics updater for all bridge diagnostic tasks.
///
/// The bridge node owns one hub so all diagnostics publish through a single
/// `diagnostic_updater::Updater`, timer, and `/diagnostics` publisher. Task
/// owners register a stable name in their constructors and must remove that
/// same name in their destructors before any callback-captured state is
/// destroyed. Future forwarder diagnostics should take this hub by reference,
/// populate a status from their own synchronized state, and unregister during
/// teardown.
class DiagnosticsManager final {
public:
  /// Create the shared updater from a ROS node-like object.
  ///
  /// @tparam NodeT Node-like type accepted by `diagnostic_updater::Updater`.
  /// @param node Node used by the updater for timers, parameters, and topics.
  template <typename NodeT>
  explicit DiagnosticsManager(NodeT&& node) : updater_(std::forward<NodeT>(node)) {
    updater_.setHardwareID("ros2_livekit_bridge");
  }

  /// Register a diagnostic task callback under a stable name.
  ///
  /// @param name Diagnostic task name.
  /// @param callback Function that populates one diagnostic status.
  void add(const std::string& name, std::function<void(diagnostic_updater::DiagnosticStatusWrapper&)> callback);

  /// Deregister a diagnostic task by name.
  ///
  /// @param name Diagnostic task name previously passed to `add`.
  void remove(const std::string& name);

private:
  diagnostic_updater::Updater updater_;
};

} // namespace ros2_livekit_bridge::diagnostics
