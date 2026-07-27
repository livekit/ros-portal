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

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "ros2_livekit_bridge/diagnostics/manager.hpp"

namespace ros2_livekit_bridge::test {

/// Wrap a shared diagnostics manager for unit-test component construction.
///
/// @param manager Shared manager that must outlive every returned callback user.
/// @return Bridge-style registration functions forwarding to @p manager.
/// @throws std::invalid_argument when @p manager is null.
inline diagnostics::DiagnosticsManagerFns makeDiagnosticsManagerFns(
    const std::shared_ptr<diagnostics::DiagnosticsManager>& manager) {
  if (!manager) {
    throw std::invalid_argument("DiagnosticsManager must not be null");
  }
  diagnostics::DiagnosticsManagerFns fns;
  fns.add = [manager](const std::string& name, diagnostics::DiagnosticsManagerFns::TaskCallback callback) {
    manager->add(name, std::move(callback));
  };
  fns.remove = [manager](const std::string& name) { manager->remove(name); };
  return fns;
}

} // namespace ros2_livekit_bridge::test
