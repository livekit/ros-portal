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

#include "ros_portal/ros_portal_manager.hpp"

#include <livekit/livekit.h>

#include <cstddef>
#include <mutex>
#include <rclcpp_components/register_node_macro.hpp>
#include <stdexcept>

#include "ros_portal/ros_portal.hpp"

namespace ros_portal {
namespace {

struct LiveKitRuntimeState {
  std::mutex mutex;
  std::size_t instance_count{0U};
  bool owned{false};
};

/// @brief Return the shared SDK lease state for this library.
LiveKitRuntimeState& runtimeState() {
  static LiveKitRuntimeState state;
  return state;
}

} // namespace

RosPortalManager::LiveKitRuntimeLease::LiveKitRuntimeLease() {
  auto& state = runtimeState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.instance_count == 0U) {
    state.owned = livekit::initialize(livekit::LogLevel::Info);
  }
  ++state.instance_count;
}

RosPortalManager::LiveKitRuntimeLease::~LiveKitRuntimeLease() {
  auto& state = runtimeState();
  const std::lock_guard<std::mutex> lock(state.mutex);
  --state.instance_count;
  if (state.instance_count == 0U && state.owned) {
    livekit::shutdown();
    state.owned = false;
  }
}

RosPortalManager::RosPortalManager(const rclcpp::NodeOptions& options)
    : RosPortalManager(options, StartupMode::Composable) {}

RosPortalManager::RosPortalManager(const rclcpp::NodeOptions& options, StartupMode mode)
    : runtime_(), portal_(std::make_shared<RosPortal>(options)) {
  // The inner node already has shared ownership, so forwarders can safely use
  // weak_from_this() without a deferred initialization callback.
  try {
    const bool initialized =
        mode == StartupMode::Standalone ? portal_->initialize() : portal_->initializeForComposition();
    if (!initialized) {
      // The component factory and standalone main translate this boundary
      // exception into a failed load or a nonzero exit status.
      throw std::runtime_error("Failed to initialize ROS Portal");
    }
    if (mode == StartupMode::Composable && portal_->rosThreads() != 0) {
      RCLCPP_WARN(portal_->get_logger(),
                  "ros_threads is ignored in composition; configure thread_num on the component container");
    }
  } catch (...) {
    portal_->shutdown();
    throw;
  }
}

RosPortalManager::~RosPortalManager() {
  // Stop SDK callbacks while a strong owner still exists. The final node
  // reference must not be released from a LiveKit delegate callback.
  portal_->shutdown();
}

// Required by the rclcpp component factory.
// NOLINTNEXTLINE(readability-identifier-naming)
rclcpp::node_interfaces::NodeBaseInterface::SharedPtr RosPortalManager::get_node_base_interface() {
  return portal_->get_node_base_interface();
}

int RosPortalManager::rosThreads() const { return portal_->rosThreads(); }

} // namespace ros_portal

RCLCPP_COMPONENTS_REGISTER_NODE(ros_portal::RosPortalManager)
