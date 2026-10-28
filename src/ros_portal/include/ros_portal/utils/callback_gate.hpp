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
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace ros_portal::utils {

/// @brief Drain callbacks before destroying their targets, including callbacks
/// already selected by an executor when a component is unloaded.
/// @note Call close() from the owning thread, never from a wrapped callback.
class CallbackGate {
public:
  /// @brief Wrap a callback without retaining its target. Closed callbacks
  /// return a default result without accessing the target.
  /// @param callback Function whose target must outlive close().
  /// @return A callable that retains admission state but does not own its target.
  template <typename ResultT, typename... Args, typename CallbackT>
  std::function<ResultT(Args...)> wrap(CallbackT callback) const {
    return [state = state_, callback = std::move(callback)](Args... args) -> ResultT {
      // Stop admitting readers immediately so a busy executor cannot starve
      // close() while it waits for the callbacks that were already running.
      if (state->closed.load()) {
        if constexpr (std::is_void_v<ResultT>) {
          return;
        } else {
          return ResultT{};
        }
      }
      const std::shared_lock<std::shared_mutex> lock(state->mutex);
      if (!state->closed.load()) {
        return callback(std::forward<Args>(args)...);
      }
      if constexpr (!std::is_void_v<ResultT>) {
        return ResultT{};
      }
    };
  }

  /// @brief Prevent new callback work and wait for running callbacks to return.
  /// Safe to call repeatedly; wrapped operations must have bounded waits.
  void close() {
    state_->closed.store(true);
    const std::unique_lock<std::shared_mutex> lock(state_->mutex);
  }

private:
  struct State {
    std::atomic_bool closed{false};
    std::shared_mutex mutex;
  };
  std::shared_ptr<State> state_{std::make_shared<State>()};
};

} // namespace ros_portal::utils
