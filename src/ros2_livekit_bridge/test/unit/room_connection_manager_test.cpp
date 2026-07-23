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

#include "ros2_livekit_bridge/room_connection_manager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <rclcpp/logger.hpp>
#include <stdexcept>

namespace ros2_livekit_bridge {
namespace {

TEST(RoomConnectionManagerTest, RequiresTryConnectMethod) {
  EXPECT_THROW((void)RoomConnectionManager({}, rclcpp::get_logger("room_connection_manager_test")),
               std::invalid_argument);
}

TEST(RoomConnectionManagerTest, AttemptsAtMostOncePerPollUntilConnected) {
  int attempts = 0;
  RoomConnectionManager manager(
      RoomConnectionManager::Methods{
          [&attempts]() {
            ++attempts;
            return attempts == 2;
          },
      },
      rclcpp::get_logger("room_connection_manager_test"));

  EXPECT_EQ(RoomConnectionManager::kRetryInterval, std::chrono::seconds(1));
  manager.poll();
  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 1);

  manager.poll();
  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(attempts, 2);

  manager.poll();
  EXPECT_EQ(attempts, 2);
}

TEST(RoomConnectionManagerTest, WaitsForRoomEosBeforeFreshConnect) {
  int attempts = 0;
  RoomConnectionManager manager(
      RoomConnectionManager::Methods{
          [&attempts]() {
            ++attempts;
            return true;
          },
      },
      rclcpp::get_logger("room_connection_manager_test"));

  manager.poll();
  ASSERT_TRUE(manager.isConnected());

  manager.onDisconnected(1U);
  EXPECT_FALSE(manager.isConnected());
  manager.poll();
  EXPECT_EQ(attempts, 1);

  manager.onRoomEos();
  manager.poll();
  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(attempts, 2);
}

TEST(RoomConnectionManagerTest, DefersToSdkDuringInSessionReconnect) {
  int attempts = 0;
  RoomConnectionManager manager(
      RoomConnectionManager::Methods{
          [&attempts]() {
            ++attempts;
            return true;
          },
      },
      rclcpp::get_logger("room_connection_manager_test"));

  manager.poll();
  ASSERT_TRUE(manager.isConnected());

  manager.onReconnecting();
  EXPECT_FALSE(manager.isConnected());
  manager.poll();
  EXPECT_EQ(attempts, 1);

  manager.onReconnected();
  EXPECT_TRUE(manager.isConnected());
  manager.poll();
  EXPECT_EQ(attempts, 1);
}

TEST(RoomConnectionManagerTest, StopSuppressesFurtherAttemptsAndCallbacks) {
  int attempts = 0;
  RoomConnectionManager manager(
      RoomConnectionManager::Methods{
          [&attempts]() {
            ++attempts;
            return true;
          },
      },
      rclcpp::get_logger("room_connection_manager_test"));

  manager.stop();
  manager.poll();
  manager.onReconnected();

  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 0);
}

} // namespace
} // namespace ros2_livekit_bridge
