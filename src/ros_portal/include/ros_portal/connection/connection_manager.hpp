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

#include <livekit/room_event_types.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <rclcpp/logger.hpp>

#include "ros_portal/connection/connection_diagnostics.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"

namespace ros_portal {

/// @brief Owns ROS Portal's room connection state, retry decisions, and the
/// session-readiness barrier used by inbound room callbacks.
///
/// ROS Portal invokes @ref poll from its fixed-rate connection timer and
/// forwards LiveKit room lifecycle callbacks to this class.
///
/// Inbound callbacks that must not run until join has finished (for example
/// data-track schema lookup) should call @ref waitForOperations. The manager
/// opens the barrier around @ref Methods::try_connect, enables operations only
/// after a successful connect/reconnect transition, and closes the session on
/// failure, disconnect, or @ref stop so waiters never hang.
class ConnectionManager {
public:
  using Clock = std::chrono::steady_clock;

  /// @brief Minimum interval between room connection attempts.
  inline static constexpr std::chrono::seconds kRetryInterval{1};

  /// @brief LiveKit-facing operation supplied by ROS Portal.
  struct Methods {
    /// @brief Make one room connection attempt.
    std::function<bool()> try_connect;
    /// @brief Return the current monotonic time for retry scheduling.
    std::function<Clock::time_point()> now = []() { return Clock::now(); };
  };

  /// @brief Construct a connection manager.
  /// @param methods Connection operation supplied by ROS Portal.
  /// @param logger Logger used for connection lifecycle messages.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register the
  /// connection-health diagnostic task.
  /// @throws std::invalid_argument when @p methods or @p diagnostics is incomplete.
  ConnectionManager(Methods methods, rclcpp::Logger logger, diagnostics::DiagnosticsManagerFns diagnostics);

  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  /// @brief Make one connection attempt when the room is disconnected.
  ///
  /// Calls are ignored while connected, while the SDK is reconnecting, while a
  /// previous attempt is active, and after a terminal disconnect until
  /// @ref onRoomEos confirms that the room has been cleaned up.
  void poll(livekit::Room& room);

  /// @brief Return whether the manager is in the connected state.
  bool isConnected() const;

  /// @brief Return whether ROS Portal components may use the LiveKit room.
  bool isOperationsEnabled() const;

  /// @brief Lifetime-safe flag mirrored by readiness transitions.
  ///
  /// Captured by room-bound callbacks that need a lock-free availability check
  /// without calling back into this manager.
  std::shared_ptr<std::atomic_bool> operationsEnabledFlag() const;

  /// @brief Block until room operations are enabled, or until the session is
  /// closed.
  /// @return True when room operations are enabled.
  bool waitForOperations();

  /// @brief Handle a LiveKit connection-state notification.
  /// @param room LiveKit room that emitted the event.
  /// @param event State change reported by the LiveKit room.
  void onConnectionStateChanged(livekit::Room& room, const livekit::ConnectionStateChangedEvent& event);

  /// @brief Handle notification that an established connection was lost and
  /// the SDK is attempting an in-session reconnect.
  /// @param room LiveKit room that emitted the event.
  /// @param event Reconnecting event reported by the LiveKit room.
  void onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent& event);

  /// @brief Handle a successful in-session reconnect.
  /// @param room LiveKit room that emitted the event.
  /// @param event Reconnected event reported by the LiveKit room.
  void onReconnected(livekit::Room& room, const livekit::ReconnectedEvent& event);

  /// @brief Handle a terminal room disconnect.
  /// @param room LiveKit room that emitted the event.
  /// @param event Terminal disconnect event reported by the LiveKit room.
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event);

  /// @brief Handle room event-stream completion and permit fresh connection
  /// attempts on the next timer tick.
  void onRoomEos();

  /// @brief Forward participant-connected events to connection diagnostics.
  void onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event);

  /// @brief Forward participant-disconnected events to connection diagnostics.
  void onParticipantDisconnected(livekit::Room& room, const livekit::ParticipantDisconnectedEvent& event);

  /// @brief Forward room-updated events to connection diagnostics.
  void onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent& event);

  /// @brief Forward participants-updated events to connection diagnostics.
  void onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent& event);

  /// @brief Stop retries, close the session barrier, and suppress shutdown-time
  /// lifecycle transitions.
  void stop();

private:
  enum class State : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    WaitingForRoomEos,
    Stopped,
  };

  /// @brief Open the session barrier for an in-flight join attempt.
  void beginSessionAttempt();

  /// @brief Enable room operations and wake waiters.
  void enableOperations();

  /// @brief Disable room operations while keeping the session open.
  void disableOperations();

  /// @brief Close the session barrier and wake waiters so they can drop work.
  void closeSession();

  /// @brief Mark a connection as available and log the transition.
  /// @param room LiveKit room that is now connected.
  void markConnectionGained(livekit::Room& room);

  /// @brief Handle notification that the SDK is attempting an in-session reconnect.
  void transitionToReconnecting();

  /// @brief Handle notification that the SDK recovered an in-session reconnect.
  /// @param room LiveKit room that recovered.
  void transitionToReconnected(livekit::Room& room);

  /// @brief Handle notification that the room terminally disconnected.
  /// @param reason LiveKit disconnect reason.
  void transitionToDisconnected(livekit::DisconnectReason reason);

  Methods methods_;
  rclcpp::Logger logger_;
  diagnostics::ConnectionHealthDiagnostics connection_diagnostics_;
  std::atomic<State> state_{State::Disconnected};
  std::atomic_bool has_connected_{false};
  Clock::time_point next_attempt_at_{Clock::time_point::min()};
  std::shared_ptr<std::atomic_bool> operations_enabled_{std::make_shared<std::atomic_bool>(false)};
  std::mutex session_mutex_;
  std::condition_variable session_cv_;
  bool session_closed_{true};
};

} // namespace ros_portal
