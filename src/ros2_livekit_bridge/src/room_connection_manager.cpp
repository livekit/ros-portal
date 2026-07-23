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

#include <exception>
#include <rclcpp/logging.hpp>
#include <stdexcept>
#include <utility>

namespace ros2_livekit_bridge {

RoomConnectionManager::RoomConnectionManager(Methods methods, rclcpp::Logger logger)
    : methods_(std::move(methods)), logger_(std::move(logger)) {
  if (!methods_.try_connect) {
    throw std::invalid_argument("RoomConnectionManager requires a try_connect method");
  }
}

void RoomConnectionManager::poll() {
  State expected = State::Disconnected;
  if (!state_.compare_exchange_strong(expected, State::Connecting)) {
    return;
  }

  bool connected = false;
  try {
    connected = methods_.try_connect();
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger_, "LiveKit room connection attempt threw: %s", error.what());
  } catch (...) {
    RCLCPP_ERROR(logger_, "LiveKit room connection attempt threw an unknown exception");
  }

  expected = State::Connecting;
  if (!connected) {
    (void)state_.compare_exchange_strong(expected, State::Disconnected);
    return;
  }

  if (state_.compare_exchange_strong(expected, State::Connected)) {
    markConnectionGained();
  }
}

bool RoomConnectionManager::isConnected() const { return state_.load() == State::Connected; }

void RoomConnectionManager::onReconnecting() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Reconnecting &&
         !state_.compare_exchange_weak(previous, State::Reconnecting)) {
  }

  if (previous == State::Connected) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost; SDK reconnecting");
  }
}

void RoomConnectionManager::onReconnected() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Connected &&
         !state_.compare_exchange_weak(previous, State::Connected)) {
  }

  if (previous != State::Stopped && previous != State::Connected) {
    markConnectionGained();
  }
}

void RoomConnectionManager::onDisconnected(std::uint32_t reason) {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::WaitingForRoomEos &&
         !state_.compare_exchange_weak(previous, State::WaitingForRoomEos)) {
  }

  if (previous == State::Connected) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost (reason=%u)", static_cast<unsigned int>(reason));
  } else if (previous == State::Reconnecting) {
    RCLCPP_WARN(logger_, "LiveKit room reconnect failed (reason=%u)", static_cast<unsigned int>(reason));
  } else if (previous == State::Connecting) {
    RCLCPP_WARN(logger_, "LiveKit room connection attempt terminated (reason=%u)", static_cast<unsigned int>(reason));
  }
}

void RoomConnectionManager::onRoomEos() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Disconnected &&
         !state_.compare_exchange_weak(previous, State::Disconnected)) {
  }

  if (previous == State::Connected) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost; room event stream ended");
  }
}

void RoomConnectionManager::stop() { state_.store(State::Stopped); }

void RoomConnectionManager::markConnectionGained() {
  if (has_connected_.exchange(true)) {
    RCLCPP_INFO(logger_, "LiveKit room connection restored");
  } else {
    RCLCPP_INFO(logger_, "Connected to LiveKit room");
  }
}

} // namespace ros2_livekit_bridge
