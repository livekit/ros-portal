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

#include "ros_portal/room_connection_manager.hpp"

#include <gtest/gtest.h>
#include <livekit/room_event_types.h>

#include <chrono>
#include <rclcpp/logger.hpp>
#include <stdexcept>
#include <utility>

namespace ros_portal {
namespace {

TEST(RoomConnectionManagerTest, RequiresTryConnectMethod) {
  EXPECT_THROW((void)RoomConnectionManager({}, rclcpp::get_logger("room_connection_manager_test")),
               std::invalid_argument);
}

TEST(RoomConnectionManagerTest, RetriesNoMoreThanOncePerInterval) {
  int attempts = 0;
  RoomConnectionManager::Clock::time_point now{};
  RoomConnectionManager manager(RoomConnectionManager::Methods{
                                    [&attempts]() {
                                      ++attempts;
                                      return attempts == 2;
                                    },
                                    [&now]() { return now; },
                                },
                                rclcpp::get_logger("room_connection_manager_test"));

  EXPECT_EQ(RoomConnectionManager::kRetryInterval, std::chrono::seconds(1));
  manager.poll();
  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 1);

  manager.poll();
  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 1);

  now += RoomConnectionManager::kRetryInterval;
  manager.poll();
  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(attempts, 2);

  manager.poll();
  EXPECT_EQ(attempts, 2);
}

TEST(RoomConnectionManagerTest, WaitsForRoomEosBeforeFreshConnect) {
  int attempts = 0;
  RoomConnectionManager::Clock::time_point now{};
  RoomConnectionManager manager(RoomConnectionManager::Methods{
                                    [&attempts]() {
                                      ++attempts;
                                      return true;
                                    },
                                    [&now]() { return now; },
                                },
                                rclcpp::get_logger("room_connection_manager_test"));

  manager.poll();
  ASSERT_TRUE(manager.isConnected());

  manager.onDisconnected(livekit::DisconnectReason::ClientInitiated);
  EXPECT_FALSE(manager.isConnected());
  manager.poll();
  EXPECT_EQ(attempts, 1);

  manager.onRoomEos();
  now += RoomConnectionManager::kRetryInterval;
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

TEST(RoomConnectionManagerTest, IgnoresReconnectNotificationsDuringInitialJoin) {
  int connected_reports = 0;
  int reconnecting_reports = 0;
  RoomConnectionManager* manager_ptr = nullptr;
  RoomConnectionManager::Methods methods;
  methods.try_connect = [&manager_ptr]() {
    manager_ptr->onConnectionStateChanged(livekit::ConnectionState::Reconnecting);
    manager_ptr->onConnectionStateChanged(livekit::ConnectionState::Connected);
    return true;
  };
  methods.report_connected = [&connected_reports]() { ++connected_reports; };
  methods.report_reconnecting = [&reconnecting_reports]() { ++reconnecting_reports; };
  RoomConnectionManager manager(std::move(methods), rclcpp::get_logger("room_connection_manager_test"));
  manager_ptr = &manager;

  manager.poll();

  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(connected_reports, 1);
  EXPECT_EQ(reconnecting_reports, 0);
}

TEST(RoomConnectionManagerTest, StopSuppressesFurtherAttemptsAndCallbacks) {
  int attempts = 0;
  int connected_reports = 0;
  RoomConnectionManager::Methods methods;
  methods.try_connect = [&attempts]() {
    ++attempts;
    return true;
  };
  methods.report_connected = [&connected_reports]() { ++connected_reports; };
  RoomConnectionManager manager(std::move(methods), rclcpp::get_logger("room_connection_manager_test"));

  manager.stop();
  manager.poll();
  manager.onReconnected();

  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 0);
  EXPECT_EQ(connected_reports, 0);
}

TEST(RoomConnectionManagerTest, ReportsEachEffectiveStateTransitionOnce) {
  int connected_reports = 0;
  int reconnecting_reports = 0;
  int disconnected_reports = 0;
  RoomConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  methods.report_connected = [&connected_reports]() { ++connected_reports; };
  methods.report_reconnecting = [&reconnecting_reports]() { ++reconnecting_reports; };
  methods.report_disconnected = [&disconnected_reports]() { ++disconnected_reports; };
  RoomConnectionManager manager(std::move(methods), rclcpp::get_logger("room_connection_manager_test"));

  manager.poll();
  EXPECT_EQ(connected_reports, 1);

  manager.onConnectionStateChanged(livekit::ConnectionState::Reconnecting);
  manager.onReconnecting();
  EXPECT_EQ(reconnecting_reports, 1);

  manager.onConnectionStateChanged(livekit::ConnectionState::Connected);
  manager.onReconnected();
  EXPECT_EQ(connected_reports, 2);

  manager.onConnectionStateChanged(livekit::ConnectionState::Disconnected);
  manager.onDisconnected(livekit::DisconnectReason::ConnectionTimeout);
  EXPECT_EQ(disconnected_reports, 1);

  manager.onRoomEos();
  EXPECT_EQ(disconnected_reports, 1);
}

TEST(RoomConnectionManagerTest, ReporterFailureDoesNotEscapeLifecycleCallback) {
  RoomConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  methods.report_connected = []() { throw std::runtime_error("diagnostics unavailable"); };
  RoomConnectionManager manager(std::move(methods), rclcpp::get_logger("room_connection_manager_test"));

  EXPECT_NO_THROW(manager.poll());
  EXPECT_TRUE(manager.isConnected());
}

} // namespace
} // namespace ros_portal
