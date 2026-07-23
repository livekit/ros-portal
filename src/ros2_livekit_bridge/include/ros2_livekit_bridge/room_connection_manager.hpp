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
#include <chrono>
#include <cstdint>
#include <functional>
#include <rclcpp/logger.hpp>

namespace ros2_livekit_bridge {

/// @brief Owns the bridge's room connection state and retry decisions.
///
/// The bridge invokes @ref poll from a 1 Hz ROS timer and forwards LiveKit room
/// lifecycle callbacks to this class. The manager intentionally knows no
/// LiveKit types; the single required SDK operation is injected through
/// @ref Methods.
class RoomConnectionManager {
public:
  /// @brief Interval used by the bridge's connection-attempt timer.
  inline static constexpr std::chrono::seconds kRetryInterval{1};

  /// @brief LiveKit-facing operation supplied by the bridge.
  struct Methods {
    /// @brief Make one room connection attempt.
    std::function<bool()> try_connect;
  };

  /// @brief Construct a room connection manager.
  /// @param methods Connection operation supplied by the bridge.
  /// @param logger Logger used for connection lifecycle messages.
  /// @throws std::invalid_argument when @p methods is incomplete.
  RoomConnectionManager(Methods methods, rclcpp::Logger logger);

  RoomConnectionManager(const RoomConnectionManager&) = delete;
  RoomConnectionManager& operator=(const RoomConnectionManager&) = delete;

  /// @brief Make one connection attempt when the room is disconnected.
  ///
  /// Calls are ignored while connected, while the SDK is reconnecting, while a
  /// previous attempt is active, and after a terminal disconnect until
  /// @ref onRoomEos confirms that the room has been cleaned up.
  void poll();

  /// @brief Return whether bridge components may use the LiveKit room.
  bool isConnected() const;

  /// @brief Handle notification that an established connection was lost and
  /// the SDK is attempting an in-session reconnect.
  void onReconnecting();

  /// @brief Handle a successful in-session reconnect.
  void onReconnected();

  /// @brief Handle a terminal room disconnect.
  /// @param reason Numeric LiveKit disconnect reason, retained as a primitive
  /// to keep LiveKit types out of this component.
  void onDisconnected(std::uint32_t reason);

  /// @brief Handle room event-stream completion and permit fresh connection
  /// attempts on the next timer tick.
  void onRoomEos();

  /// @brief Stop retries and suppress shutdown-time lifecycle transitions.
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

  /// @brief Mark a connection as available and log the transition.
  void markConnectionGained();

  Methods methods_;
  rclcpp::Logger logger_;
  std::atomic<State> state_{State::Disconnected};
  std::atomic_bool has_connected_{false};
};

} // namespace ros2_livekit_bridge
