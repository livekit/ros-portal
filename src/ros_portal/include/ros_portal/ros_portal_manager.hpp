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
#include <rclcpp/rclcpp.hpp>

namespace ros_portal {

class RosPortal;

/// @brief Owns a ROS Portal node and its process-wide LiveKit runtime lease.
///
/// The component container or standalone executable owns the executor. Destroy
/// the manager only after removing its node from that executor.
class RosPortalManager final {
public:
  /// @brief Construct and initialize an owned node without connecting to LiveKit.
  /// @param options Options forwarded to the owned ROS Portal node.
  /// @throw std::runtime_error if local ROS Portal initialization fails.
  explicit RosPortalManager(const rclcpp::NodeOptions& options);

  /// @brief Stop and destroy ROS Portal before releasing the LiveKit runtime.
  ~RosPortalManager();

  RosPortalManager(const RosPortalManager&) = delete;
  RosPortalManager& operator=(const RosPortalManager&) = delete;
  RosPortalManager(RosPortalManager&&) = delete;
  RosPortalManager& operator=(RosPortalManager&&) = delete;

  /// @brief Return the owned node interface for the caller's executor.
  /// @return ROS Portal's node base interface, including container overrides.
  /// @note The method name is required by the rclcpp component factory.
  // NOLINTNEXTLINE(readability-identifier-naming)
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr get_node_base_interface();

  /// @brief Return the configured worker count for a standalone executor.
  /// @return The ros_threads setting; zero lets rclcpp choose the worker count.
  int rosThreads() const;

private:
  /// @brief Shares SDK ownership across managers in this library.
  class LiveKitRuntimeLease {
  public:
    /// @brief Acquire a lease, initializing the SDK for the first owner.
    LiveKitRuntimeLease();
    /// @brief Release the lease and stop a manager-owned SDK after the last owner.
    ~LiveKitRuntimeLease();

    LiveKitRuntimeLease(const LiveKitRuntimeLease&) = delete;
    LiveKitRuntimeLease& operator=(const LiveKitRuntimeLease&) = delete;
    LiveKitRuntimeLease(LiveKitRuntimeLease&&) = delete;
    LiveKitRuntimeLease& operator=(LiveKitRuntimeLease&&) = delete;
  };

  // Member order keeps the runtime alive through node destruction, including
  // constructor failure. shutdown() runs while portal_ is still a strong owner.
  LiveKitRuntimeLease runtime_;
  std::shared_ptr<RosPortal> portal_;
};

} // namespace ros_portal
