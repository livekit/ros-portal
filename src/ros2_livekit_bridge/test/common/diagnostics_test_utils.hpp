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
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "ros2_livekit_bridge/diagnostics/diagnostics_fns.hpp"

namespace ros2_livekit_bridge::test {

/// Wrap a shared diagnostics updater for unit-test component construction.
///
/// @param updater Shared updater that must outlive every returned callback user.
/// @return Bridge-style registration functions forwarding to @p updater.
/// @throws std::invalid_argument when @p updater is null.
inline diagnostics::DiagnosticsManagerFns makeDiagnosticsFns(
    const std::shared_ptr<diagnostic_updater::Updater>& updater) {
  if (!updater) {
    throw std::invalid_argument("diagnostic_updater::Updater must not be null");
  }
  diagnostics::DiagnosticsManagerFns fns;
  fns.add = [updater](const std::string& name, diagnostics::DiagnosticsManagerFns::TaskCallback callback) {
    updater->removeByName(name);
    updater->add(name, std::move(callback));
  };
  fns.remove = [updater](const std::string& name) { updater->removeByName(name); };
  return fns;
}

} // namespace ros2_livekit_bridge::test
