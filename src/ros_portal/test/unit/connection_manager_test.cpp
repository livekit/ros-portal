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

#include "ros_portal/connection/connection_manager.hpp"

#include <gtest/gtest.h>
#include <livekit/room.h>
#include <livekit/room_event_types.h>

#include <atomic>
#include <chrono>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <future>
#include <optional>
#include <rclcpp/logger.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace ros_portal {
namespace {

diagnostics::DiagnosticsManagerFns makeDiagnosticsFns(int* add_count = nullptr, int* remove_count = nullptr) {
  diagnostics::DiagnosticsManagerFns diagnostics;
  diagnostics.add = [add_count](const std::string&, diagnostics::DiagnosticsManagerFns::TaskCallback) {
    if (add_count) {
      ++(*add_count);
    }
  };
  diagnostics.remove = [remove_count](const std::string&) {
    if (remove_count) {
      ++(*remove_count);
    }
  };
  return diagnostics;
}

/// @brief Call waitForOperations() off-thread so a barrier regression fails the
/// test instead of hanging it. On timeout the waiter is released through a
/// pause so the future can be joined.
bool waitForOperationsWithoutHanging(ConnectionManager& manager) {
  auto waiter = std::async(std::launch::async, [&manager]() { return manager.waitForOperations(); });
  if (waiter.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
    ADD_FAILURE() << "waitForOperations() blocked instead of releasing the waiter";
    (void)manager.pauseForwarding();
  }
  return waiter.get();
}

livekit::ConnectionStateChangedEvent makeConnectionStateChangedEvent(livekit::ConnectionState state) {
  livekit::ConnectionStateChangedEvent event;
  event.state = state;
  return event;
}

livekit::DisconnectedEvent makeDisconnectedEvent(livekit::DisconnectReason reason) {
  livekit::DisconnectedEvent event;
  event.reason = reason;
  return event;
}

TEST(ConnectionManagerTest, RequiresTryConnectMethod) {
  EXPECT_THROW((void)ConnectionManager({}, rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns()),
               std::invalid_argument);
}

TEST(ConnectionManagerTest, RegistersAndRemovesConnectionHealthDiagnostics) {
  int add_count = 0;
  int remove_count = 0;

  {
    ConnectionManager manager(ConnectionManager::Methods{[]() { return false; }},
                              rclcpp::get_logger("connection_manager_test"),
                              makeDiagnosticsFns(&add_count, &remove_count));
    EXPECT_EQ(add_count, 1);
    EXPECT_EQ(remove_count, 0);
  }

  EXPECT_EQ(remove_count, 1);
}

TEST(ConnectionManagerTest, RetriesNoMoreThanOncePerInterval) {
  int attempts = 0;
  ConnectionManager::Clock::time_point now{};
  livekit::Room room;
  ConnectionManager manager(ConnectionManager::Methods{
                                [&attempts]() {
                                  ++attempts;
                                  return attempts == 2;
                                },
                                [&now]() { return now; },
                            },
                            rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  EXPECT_EQ(ConnectionManager::kRetryInterval, std::chrono::seconds(1));
  manager.poll(room);
  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 1);

  manager.poll(room);
  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 1);

  now += ConnectionManager::kRetryInterval;
  manager.poll(room);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(attempts, 2);

  manager.poll(room);
  EXPECT_EQ(attempts, 2);
}

TEST(ConnectionManagerTest, WaitsForRoomEosBeforeFreshConnect) {
  int attempts = 0;
  ConnectionManager::Clock::time_point now{};
  livekit::Room room;
  ConnectionManager manager(ConnectionManager::Methods{
                                [&attempts]() {
                                  ++attempts;
                                  return true;
                                },
                                [&now]() { return now; },
                            },
                            rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isConnected());

  const auto disconnected_event = makeDisconnectedEvent(livekit::DisconnectReason::ClientInitiated);
  manager.onDisconnected(room, disconnected_event);
  EXPECT_FALSE(manager.isConnected());
  manager.poll(room);
  EXPECT_EQ(attempts, 1);

  manager.onRoomEos();
  now += ConnectionManager::kRetryInterval;
  manager.poll(room);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_EQ(attempts, 2);
}

TEST(ConnectionManagerTest, DefersToSdkDuringInSessionReconnect) {
  int attempts = 0;
  livekit::Room room;
  const livekit::ReconnectingEvent reconnecting_event;
  const livekit::ReconnectedEvent reconnected_event;
  ConnectionManager manager(
      ConnectionManager::Methods{
          [&attempts]() {
            ++attempts;
            return true;
          },
      },
      rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isConnected());

  manager.onReconnecting(room, reconnecting_event);
  EXPECT_FALSE(manager.isConnected());
  manager.poll(room);
  EXPECT_EQ(attempts, 1);

  manager.onReconnected(room, reconnected_event);
  EXPECT_TRUE(manager.isConnected());
  manager.poll(room);
  EXPECT_EQ(attempts, 1);
}

TEST(ConnectionManagerTest, IgnoresReconnectNotificationsDuringInitialJoin) {
  ConnectionManager* manager_ptr = nullptr;
  livekit::Room room;
  ConnectionManager::Methods methods;
  methods.try_connect = [&manager_ptr, &room]() {
    const auto reconnecting_event = makeConnectionStateChangedEvent(livekit::ConnectionState::Reconnecting);
    const auto connected_event = makeConnectionStateChangedEvent(livekit::ConnectionState::Connected);
    manager_ptr->onConnectionStateChanged(room, reconnecting_event);
    manager_ptr->onConnectionStateChanged(room, connected_event);
    return true;
  };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());
  manager_ptr = &manager;

  manager.poll(room);

  EXPECT_TRUE(manager.isConnected());
  EXPECT_TRUE(manager.isOperationsEnabled());
}

TEST(ConnectionManagerTest, StopSuppressesFurtherAttemptsAndCallbacks) {
  int attempts = 0;
  livekit::Room room;
  const livekit::ReconnectedEvent reconnected_event;
  ConnectionManager::Methods methods;
  methods.try_connect = [&attempts]() {
    ++attempts;
    return true;
  };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.stop();
  manager.poll(room);
  manager.onReconnected(room, reconnected_event);

  EXPECT_FALSE(manager.isConnected());
  EXPECT_EQ(attempts, 0);
}

TEST(ConnectionManagerTest, HandlesDuplicateLifecycleNotificationsOnce) {
  livekit::Room room;
  const auto reconnecting_state = makeConnectionStateChangedEvent(livekit::ConnectionState::Reconnecting);
  const auto connected_state = makeConnectionStateChangedEvent(livekit::ConnectionState::Connected);
  const auto disconnected_state = makeConnectionStateChangedEvent(livekit::ConnectionState::Disconnected);
  const livekit::ReconnectingEvent reconnecting_event;
  const livekit::ReconnectedEvent reconnected_event;
  const auto disconnected_event = makeDisconnectedEvent(livekit::DisconnectReason::ConnectionTimeout);
  ConnectionManager manager(ConnectionManager::Methods{[]() { return true; }},
                            rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isConnected());

  manager.onConnectionStateChanged(room, reconnecting_state);
  manager.onReconnecting(room, reconnecting_event);
  EXPECT_FALSE(manager.isConnected());

  manager.onConnectionStateChanged(room, connected_state);
  manager.onReconnected(room, reconnected_event);
  EXPECT_TRUE(manager.isConnected());

  manager.onConnectionStateChanged(room, disconnected_state);
  manager.onDisconnected(room, disconnected_event);
  EXPECT_FALSE(manager.isConnected());

  manager.onRoomEos();
  EXPECT_FALSE(manager.isConnected());
}

TEST(ConnectionManagerTest, WaitForOperationsUnblocksAfterSuccessfulConnect) {
  std::atomic_bool waiter_started{false};
  std::atomic_bool waiter_finished{false};
  std::atomic_bool saw_ready{false};
  std::thread waiter;
  ConnectionManager* manager_ptr = nullptr;
  livekit::Room room;

  ConnectionManager::Methods methods;
  methods.try_connect = [&]() {
    waiter = std::thread([&]() {
      waiter_started.store(true);
      saw_ready.store(manager_ptr->waitForOperations());
      waiter_finished.store(true);
    });
    while (!waiter_started.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(waiter_finished.load());
    EXPECT_FALSE(manager_ptr->isOperationsEnabled());
    return true;
  };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());
  manager_ptr = &manager;

  manager.poll(room);
  waiter.join();

  EXPECT_TRUE(manager.isConnected());
  EXPECT_TRUE(manager.isOperationsEnabled());
  EXPECT_TRUE(saw_ready.load());
  EXPECT_TRUE(waiter_finished.load());
}

TEST(ConnectionManagerTest, FailedConnectClosesSessionForWaiters) {
  std::atomic_bool waiter_started{false};
  std::atomic_bool saw_ready{true};
  std::thread waiter;
  ConnectionManager* manager_ptr = nullptr;
  livekit::Room room;

  ConnectionManager::Methods methods;
  methods.try_connect = [&]() {
    waiter = std::thread([&]() {
      waiter_started.store(true);
      saw_ready.store(manager_ptr->waitForOperations());
    });
    while (!waiter_started.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return false;
  };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());
  manager_ptr = &manager;

  manager.poll(room);
  waiter.join();

  EXPECT_FALSE(manager.isConnected());
  EXPECT_FALSE(manager.isOperationsEnabled());
  EXPECT_FALSE(saw_ready.load());
}

TEST(ConnectionManagerTest, ReconnectKeepsWaitersBlockedUntilRestored) {
  livekit::Room room;
  const livekit::ReconnectingEvent reconnecting_event;
  const livekit::ReconnectedEvent reconnected_event;
  ConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isOperationsEnabled());

  manager.onReconnecting(room, reconnecting_event);
  EXPECT_FALSE(manager.isOperationsEnabled());

  std::atomic_bool waiter_finished{false};
  std::atomic_bool saw_ready{false};
  std::thread waiter([&]() {
    saw_ready.store(manager.waitForOperations());
    waiter_finished.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(waiter_finished.load());

  manager.onReconnected(room, reconnected_event);
  waiter.join();

  EXPECT_TRUE(manager.isOperationsEnabled());
  EXPECT_TRUE(saw_ready.load());
}

TEST(ConnectionManagerTest, StopClosesSessionForWaiters) {
  livekit::Room room;
  const livekit::ReconnectingEvent reconnecting_event;
  ConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isOperationsEnabled());

  manager.onReconnecting(room, reconnecting_event);
  std::atomic_bool saw_ready{true};
  std::thread waiter([&]() { saw_ready.store(manager.waitForOperations()); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  manager.stop();
  waiter.join();

  EXPECT_FALSE(manager.isOperationsEnabled());
  EXPECT_FALSE(saw_ready.load());
}

TEST(ConnectionManagerTest, PauseAndResumeForwardingAreIdempotent) {
  livekit::Room room;
  ConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isOperationsEnabled());
  EXPECT_FALSE(manager.isForwardingPaused());

  EXPECT_TRUE(manager.pauseForwarding());
  EXPECT_TRUE(manager.isForwardingPaused());
  EXPECT_FALSE(manager.isOperationsEnabled());
  // The room connection is kept so operations can resume without a fresh join.
  EXPECT_TRUE(manager.isConnected());

  EXPECT_TRUE(manager.pauseForwarding());
  EXPECT_TRUE(manager.isForwardingPaused());
  EXPECT_FALSE(manager.isOperationsEnabled());

  EXPECT_TRUE(manager.resumeForwarding());
  EXPECT_FALSE(manager.isForwardingPaused());
  EXPECT_TRUE(manager.isOperationsEnabled());

  EXPECT_TRUE(manager.resumeForwarding());
  EXPECT_TRUE(manager.isOperationsEnabled());
}

TEST(ConnectionManagerTest, PausedOperationsSurviveConnectAndReconnect) {
  int attempts = 0;
  ConnectionManager::Clock::time_point now{};
  livekit::Room room;
  const livekit::ReconnectingEvent reconnecting_event;
  const livekit::ReconnectedEvent reconnected_event;
  ConnectionManager manager(ConnectionManager::Methods{
                                [&attempts]() {
                                  ++attempts;
                                  return true;
                                },
                                [&now]() { return now; },
                            },
                            rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  EXPECT_TRUE(manager.pauseForwarding());

  // A fresh connect must not lift the pause.
  manager.poll(room);
  EXPECT_EQ(attempts, 1);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_FALSE(manager.isOperationsEnabled());

  // Neither must an in-session reconnect, and the barrier must stay closed
  // while the SDK is reconnecting.
  manager.onReconnecting(room, reconnecting_event);
  EXPECT_FALSE(waitForOperationsWithoutHanging(manager));
  manager.onReconnected(room, reconnected_event);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_FALSE(manager.isOperationsEnabled());

  EXPECT_TRUE(manager.resumeForwarding());
  EXPECT_TRUE(manager.isOperationsEnabled());
}

TEST(ConnectionManagerTest, PausedOperationsReleaseWaitersInsteadOfBlocking) {
  livekit::Room room;
  ConnectionManager::Methods methods;
  methods.try_connect = []() { return true; };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  manager.poll(room);
  ASSERT_TRUE(manager.isOperationsEnabled());

  EXPECT_TRUE(manager.pauseForwarding());

  // Paused operations behave like a closed session: waiters are released with a
  // negative answer instead of blocking until an operator resumes.
  EXPECT_FALSE(waitForOperationsWithoutHanging(manager));
}

TEST(ConnectionManagerTest, PausedConnectionAttemptDoesNotBlockWaiters) {
  livekit::Room room;
  ConnectionManager* manager_ptr = nullptr;
  std::optional<bool> saw_ready_during_attempt;

  ConnectionManager::Methods methods;
  methods.try_connect = [&]() {
    // Inbound room callbacks can arrive while try_connect is still joining.
    saw_ready_during_attempt = waitForOperationsWithoutHanging(*manager_ptr);
    return true;
  };
  ConnectionManager manager(std::move(methods), rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());
  manager_ptr = &manager;

  EXPECT_TRUE(manager.pauseForwarding());
  manager.poll(room);

  ASSERT_TRUE(saw_ready_during_attempt.has_value());
  EXPECT_FALSE(*saw_ready_during_attempt);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_TRUE(manager.isForwardingPaused());
  EXPECT_FALSE(manager.isOperationsEnabled());
}

TEST(ConnectionManagerTest, ResumeForwardingWithoutConnectionDefersUntilConnected) {
  int attempts = 0;
  ConnectionManager::Clock::time_point now{};
  livekit::Room room;
  ConnectionManager manager(ConnectionManager::Methods{
                                [&attempts]() {
                                  ++attempts;
                                  return attempts > 1;
                                },
                                [&now]() { return now; },
                            },
                            rclcpp::get_logger("connection_manager_test"), makeDiagnosticsFns());

  EXPECT_TRUE(manager.pauseForwarding());
  // Not connected yet: the pause is lifted, but operations can only be enabled
  // once a connection is established.
  EXPECT_FALSE(manager.resumeForwarding());
  EXPECT_FALSE(manager.isForwardingPaused());
  EXPECT_FALSE(manager.isOperationsEnabled());

  manager.poll(room);
  EXPECT_FALSE(manager.isConnected());
  EXPECT_FALSE(manager.isOperationsEnabled());

  now += ConnectionManager::kRetryInterval;
  manager.poll(room);
  EXPECT_TRUE(manager.isConnected());
  EXPECT_TRUE(manager.isOperationsEnabled());
}

} // namespace
} // namespace ros_portal
