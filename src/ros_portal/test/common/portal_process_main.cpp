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
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <string>

#include "ros_portal/ros_portal.hpp"
#include "ros_portal/utils/ros_utils.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int result = EXIT_SUCCESS;
  try {
    auto portal = std::make_shared<ros_portal::RosPortal>();
    const auto identity_a = ros_portal::utils::environmentVariable("LIVEKIT_IDENTITY_A").value_or("ros-portal-test-a");
    const auto identity_b = ros_portal::utils::environmentVariable("LIVEKIT_IDENTITY_B").value_or("ros-portal-test-b");
    auto participants = portal->create_service<std_srvs::srv::Trigger>(
        "_test/participants",
        [&portal, &identity_a, &identity_b](const std::shared_ptr<std_srvs::srv::Trigger::Request>&,
                                            const std::shared_ptr<std_srvs::srv::Trigger::Response>& response) {
          response->success = true;
          for (const auto& identity : {identity_a, identity_b}) {
            if (portal->hasParticipant(identity)) {
              response->message += "|" + identity + "|";
            }
          }
        });
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions{}, 2);
    executor.add_node(portal);
    executor.spin();
    executor.remove_node(portal);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger("portal_test_process"), "%s", error.what());
    result = EXIT_FAILURE;
  }
  rclcpp::shutdown();
  return result;
}
