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

#include <atomic>

namespace ros_portal::utils {

/// @brief Mark a reader thread alive for the lifetime of this guard.
///
/// Diagnostics compare the flag with the number of active tracks to detect
/// reader threads that stopped while their track is still registered.
class ReaderAliveGuard {
public:
  /// @brief Set @p alive to true until this guard is destroyed.
  explicit ReaderAliveGuard(std::atomic_bool& alive) : alive_(alive) { alive_.store(true, std::memory_order_relaxed); }
  ~ReaderAliveGuard() { alive_.store(false, std::memory_order_relaxed); }

  ReaderAliveGuard(const ReaderAliveGuard&) = delete;
  ReaderAliveGuard& operator=(const ReaderAliveGuard&) = delete;
  ReaderAliveGuard(ReaderAliveGuard&&) = delete;
  ReaderAliveGuard& operator=(ReaderAliveGuard&&) = delete;

private:
  std::atomic_bool& alive_;
};

} // namespace ros_portal::utils
