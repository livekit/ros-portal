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

#include <exception>
#include <rclcpp/logging.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ros_portal {
namespace {

constexpr std::string_view disconnectReasonName(livekit::DisconnectReason reason) {
  switch (reason) {
    case livekit::DisconnectReason::Unknown:
      return "Unknown";
    case livekit::DisconnectReason::ClientInitiated:
      return "ClientInitiated";
    case livekit::DisconnectReason::DuplicateIdentity:
      return "DuplicateIdentity";
    case livekit::DisconnectReason::ServerShutdown:
      return "ServerShutdown";
    case livekit::DisconnectReason::ParticipantRemoved:
      return "ParticipantRemoved";
    case livekit::DisconnectReason::RoomDeleted:
      return "RoomDeleted";
    case livekit::DisconnectReason::StateMismatch:
      return "StateMismatch";
    case livekit::DisconnectReason::JoinFailure:
      return "JoinFailure";
    case livekit::DisconnectReason::Migration:
      return "Migration";
    case livekit::DisconnectReason::SignalClose:
      return "SignalClose";
    case livekit::DisconnectReason::RoomClosed:
      return "RoomClosed";
    case livekit::DisconnectReason::UserUnavailable:
      return "UserUnavailable";
    case livekit::DisconnectReason::UserRejected:
      return "UserRejected";
    case livekit::DisconnectReason::SipTrunkFailure:
      return "SipTrunkFailure";
    case livekit::DisconnectReason::ConnectionTimeout:
      return "ConnectionTimeout";
    case livekit::DisconnectReason::MediaFailure:
      return "MediaFailure";
    case livekit::DisconnectReason::AgentError:
      return "AgentError";
  }
  return "Unknown";
}

} // namespace

RoomConnectionManager::RoomConnectionManager(Methods methods, rclcpp::Logger logger)
    : methods_(std::move(methods)), logger_(std::move(logger)) {
  if (!methods_.try_connect || !methods_.now) {
    throw std::invalid_argument("RoomConnectionManager requires try_connect and now methods");
  }
}

void RoomConnectionManager::poll() {
  if (state_.load() != State::Disconnected) {
    return;
  }

  const auto now = methods_.now();
  if (now < next_attempt_at_) {
    return;
  }

  State expected = State::Disconnected;
  if (!state_.compare_exchange_strong(expected, State::Connecting)) {
    return;
  }
  next_attempt_at_ = now + kRetryInterval;

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
    reportTransition(methods_.report_connected);
  }
}

bool RoomConnectionManager::isConnected() const { return state_.load() == State::Connected; }

void RoomConnectionManager::onConnectionStateChanged(livekit::ConnectionState state) {
  switch (state) {
    case livekit::ConnectionState::Connected:
      onReconnected();
      break;
    case livekit::ConnectionState::Reconnecting:
      onReconnecting();
      break;
    case livekit::ConnectionState::Disconnected:
      onDisconnected(livekit::DisconnectReason::Unknown);
      break;
  }
}

void RoomConnectionManager::onReconnecting() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Reconnecting &&
         !state_.compare_exchange_weak(previous, State::Reconnecting)) {
  }

  if (previous != State::Stopped && previous != State::Reconnecting) {
    if (previous == State::Connected) {
      RCLCPP_WARN(logger_, "LiveKit room connection lost; SDK reconnecting");
    }
    reportTransition(methods_.report_reconnecting);
  }
}

void RoomConnectionManager::onReconnected() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Connected &&
         !state_.compare_exchange_weak(previous, State::Connected)) {
  }

  if (previous != State::Stopped && previous != State::Connected) {
    markConnectionGained();
    reportTransition(methods_.report_connected);
  }
}

void RoomConnectionManager::onDisconnected(livekit::DisconnectReason reason) {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::WaitingForRoomEos &&
         !state_.compare_exchange_weak(previous, State::WaitingForRoomEos)) {
  }

  const auto reason_name = disconnectReasonName(reason);
  if (previous == State::Connected) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost (reason=%.*s)", static_cast<int>(reason_name.size()),
                reason_name.data());
  } else if (previous == State::Reconnecting) {
    RCLCPP_WARN(logger_, "LiveKit room reconnect failed (reason=%.*s)", static_cast<int>(reason_name.size()),
                reason_name.data());
  } else if (previous == State::Connecting) {
    RCLCPP_WARN(logger_, "LiveKit room connection attempt terminated (reason=%.*s)",
                static_cast<int>(reason_name.size()), reason_name.data());
  }

  if (previous != State::Stopped && previous != State::WaitingForRoomEos && previous != State::Disconnected) {
    reportTransition(methods_.report_disconnected);
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
  if (previous != State::Stopped && previous != State::Disconnected && previous != State::WaitingForRoomEos) {
    reportTransition(methods_.report_disconnected);
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

void RoomConnectionManager::reportTransition(const std::function<void()>& reporter) {
  if (!reporter) {
    return;
  }

  try {
    reporter();
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger_, "Connection-state reporter threw: %s", error.what());
  } catch (...) {
    RCLCPP_ERROR(logger_, "Connection-state reporter threw an unknown exception");
  }
}

} // namespace ros_portal
