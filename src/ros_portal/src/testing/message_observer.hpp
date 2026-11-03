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
#include <rclcpp/message_info.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace ros_portal::test {

/// @brief Test-build-only observations at the real ROS message boundaries.
/// @note Install before creating a portal; clear only after its readers stop.
/// Callbacks may run on the executor or inbound-video reader thread.
class MessageObserver {
public:
  virtual ~MessageObserver() = default;
  /// @brief Observe an immutable image at subscription callback entry.
  virtual void imageReceived(const sensor_msgs::msg::Image& message, const rclcpp::MessageInfo& info) noexcept = 0;
  /// @brief Observe the portal's own compressed image immediately before publication.
  virtual void imagePublishing(const sensor_msgs::msg::CompressedImage& message) noexcept = 0;
};

/// @brief Non-owning observer, present only in BUILD_TESTING builds.
extern std::atomic<MessageObserver*> message_observer;

} // namespace ros_portal::test
