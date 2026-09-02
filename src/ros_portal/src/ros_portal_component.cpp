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

#include <livekit/livekit.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>

#include "ros_portal/ros_portal.hpp"

namespace ros_portal {
namespace {

/// Owns the process-global LiveKit runtime while ROS Portal components exist.
///
/// This base is constructed before and destroyed after the RosPortal base, so
/// every LiveKit object is released before the component-owned runtime stops.
class LiveKitRuntimeGuard {
public:
  LiveKitRuntimeGuard() {
    const std::lock_guard<std::mutex> lock(runtimeMutex());
    if (instanceCount() == 0U) {
      runtimeOwnedByComponents() = livekit::initialize(livekit::LogLevel::Info);
    }
    ++instanceCount();
  }

  virtual ~LiveKitRuntimeGuard() {
    const std::lock_guard<std::mutex> lock(runtimeMutex());
    if (instanceCount() == 0U) {
      return;
    }

    --instanceCount();
    if (instanceCount() == 0U && runtimeOwnedByComponents()) {
      livekit::shutdown();
      runtimeOwnedByComponents() = false;
    }
  }

  LiveKitRuntimeGuard(const LiveKitRuntimeGuard&) = delete;
  LiveKitRuntimeGuard& operator=(const LiveKitRuntimeGuard&) = delete;
  LiveKitRuntimeGuard(LiveKitRuntimeGuard&&) = delete;
  LiveKitRuntimeGuard& operator=(LiveKitRuntimeGuard&&) = delete;

private:
  static std::mutex& runtimeMutex() {
    static std::mutex mutex;
    return mutex;
  }

  static std::size_t& instanceCount() {
    static std::size_t count = 0U;
    return count;
  }

  static bool& runtimeOwnedByComponents() {
    static bool owned = false;
    return owned;
  }
};

} // namespace

/// ROS Portal entry point for rclcpp component containers.
class RosPortalComponent final : private LiveKitRuntimeGuard, public RosPortal {
public:
  /// @brief Construct a composable ROS Portal node.
  /// @param options Options supplied by the component container.
  explicit RosPortalComponent(const rclcpp::NodeOptions& options)
      : LiveKitRuntimeGuard(),
        RosPortal(options),
        initialization_timer_(create_wall_timer(std::chrono::milliseconds(1), [this]() { initializeNode(); })) {}

private:
  /// @brief Initialize after the component manager owns this node.
  void initializeNode() {
    initialization_timer_->cancel();

    try {
      if (!initializeForComposition()) {
        RCLCPP_FATAL(get_logger(), "Failed to initialize composable ROS Portal");
        return;
      }

      if (rosThreads() != 0) {
        RCLCPP_WARN(get_logger(),
                    "ros_threads is ignored in composition; configure thread_num on the component container");
      }
    } catch (const std::exception& error) {
      RCLCPP_FATAL(get_logger(), "Unhandled exception while initializing composable ROS Portal: %s", error.what());
    } catch (...) {
      RCLCPP_FATAL(get_logger(), "Unknown exception while initializing composable ROS Portal");
    }
  }

  /// One-shot timer that defers initialization until shared ownership exists.
  rclcpp::TimerBase::SharedPtr initialization_timer_;
};

} // namespace ros_portal

RCLCPP_COMPONENTS_REGISTER_NODE(ros_portal::RosPortalComponent)
