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

#include <cstdlib>
#include <exception>
#include <iostream>

#include <rclcpp/rclcpp.hpp>

#include <livekit/livekit.h>

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"

namespace
{
// RAII owner of the process-global LiveKit SDK lifecycle. livekit::initialize()
// must be the first LiveKit API called in the process, and the matching
// shutdown() must run after every LiveKit object (the bridge's room) has been
// destroyed. Declaring this before the node guarantees that ordering on every
// exit path, including exceptions.
struct LiveKitSdkScope
{
  LiveKitSdkScope() { livekit::initialize(livekit::LogLevel::Info); }
  ~LiveKitSdkScope() { livekit::shutdown(); }
  LiveKitSdkScope(const LiveKitSdkScope &) = delete;
  LiveKitSdkScope & operator=(const LiveKitSdkScope &) = delete;
};
}  // namespace

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  const auto logger = rclcpp::get_logger("ros2_livekit_bridge_node");

  LiveKitSdkScope livekit_sdk_scope;

  try {
    auto node = std::make_shared<ros2_livekit_bridge::Ros2LiveKitBridge>();
    if (!node->initialize()) {
      RCLCPP_FATAL(logger, "Failed to initialize ROS2 LiveKit bridge");
      rclcpp::shutdown();
      return EXIT_FAILURE;
    }

    rclcpp::ExecutorOptions exec_options;
    const size_t num_threads =
      node->ros_threads() > 0 ? static_cast<size_t>(node->ros_threads()) : 0;

    std::cout << "Starting executor with " << num_threads << " threads"
              << std::endl;
    rclcpp::executors::MultiThreadedExecutor executor(
      exec_options, num_threads);
    executor.add_node(node);
    executor.spin();
  } catch (const std::exception & e) {
    RCLCPP_FATAL(logger, "Unhandled exception in ROS2 LiveKit bridge: %s",
                 e.what());
    rclcpp::shutdown();
    return EXIT_FAILURE;
  } catch (...) {
    RCLCPP_FATAL(logger, "Unknown unhandled exception in ROS2 LiveKit bridge");
    rclcpp::shutdown();
    return EXIT_FAILURE;
  }

  rclcpp::shutdown();

  return EXIT_SUCCESS;
}
