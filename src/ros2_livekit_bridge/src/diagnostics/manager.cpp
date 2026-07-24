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

#include "ros2_livekit_bridge/diagnostics/manager.hpp"

#include <utility>

namespace ros2_livekit_bridge::diagnostics {

void DiagnosticsManager::add(const std::string& name,
                             std::function<void(diagnostic_updater::DiagnosticStatusWrapper&)> callback) {
  updater_.removeByName(name);
  updater_.add(name, std::move(callback));
}

void DiagnosticsManager::remove(const std::string& name) { updater_.removeByName(name); }

} // namespace ros2_livekit_bridge::diagnostics
