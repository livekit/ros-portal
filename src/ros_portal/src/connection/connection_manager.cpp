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

#include <livekit/local_participant.h>
#include <livekit/room.h>

#include <exception>
#include <rclcpp/logging.hpp>
#include <stdexcept>
#include <string>
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

ConnectionManager::ConnectionManager(Methods methods, rclcpp::Logger logger,
                                     diagnostics::DiagnosticsManagerFns diagnostics)
    : methods_(std::move(methods)), logger_(std::move(logger)), connection_diagnostics_(std::move(diagnostics)) {
  if (!methods_.try_connect || !methods_.now) {
    throw std::invalid_argument("ConnectionManager requires try_connect and now methods");
  }
}

void ConnectionManager::poll(livekit::Room& room) {
  connection_diagnostics_.pollStats(room);

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

  // Open the barrier before try_connect so DataTrackPublished callbacks that
  // fire during join wait for enableOperations() instead of racing schema
  // lookup while operations are still disabled.
  beginSessionAttempt();

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
    closeSession();
    (void)state_.compare_exchange_strong(expected, State::Disconnected);
    return;
  }

  if (state_.compare_exchange_strong(expected, State::Connected)) {
    markConnectionGained(room);
    enableOperations();
    connection_diagnostics_.markConnected(room);
  } else {
    closeSession();
  }
}

bool ConnectionManager::isConnected() const { return state_.load() == State::Connected; }

bool ConnectionManager::isOperationsEnabled() const { return operations_enabled_->load(); }

std::shared_ptr<std::atomic_bool> ConnectionManager::operationsEnabledFlag() const { return operations_enabled_; }

bool ConnectionManager::resumeForwarding() {
  bool was_paused = false;
  bool enabled = false;
  {
    const std::lock_guard<std::mutex> lock(session_mutex_);
    was_paused = forwarding_paused_;
    forwarding_paused_ = false;
    if (state_.load() == State::Connected) {
      session_closed_ = false;
      operations_enabled_->store(true);
    }
    enabled = operations_enabled_->load();
  }
  session_cv_.notify_all();

  if (was_paused) {
    RCLCPP_INFO(logger_, "ROS Portal forwardiing operations resumed%s",
                enabled ? "" : "; waiting for a room connection");
  }
  return enabled;
}

bool ConnectionManager::pauseForwarding() {
  bool was_paused = false;
  {
    const std::lock_guard<std::mutex> lock(session_mutex_);
    was_paused = forwarding_paused_;
    forwarding_paused_ = true;
    operations_enabled_->store(false);
    // Treat the pause like a closed session so waiters drop work instead of
    // blocking until the pause is lifted.
    session_closed_ = true;
  }
  session_cv_.notify_all();

  if (!was_paused) {
    RCLCPP_INFO(logger_, "ROS Portal forwarding operations paused");
  }
  return true;
}

bool ConnectionManager::isForwardingPaused() const {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  return forwarding_paused_;
}

bool ConnectionManager::waitForOperations() {
  std::unique_lock<std::mutex> lock(session_mutex_);
  session_cv_.wait(lock, [this]() { return operations_enabled_->load() || session_closed_; });
  return operations_enabled_->load();
}

void ConnectionManager::onConnectionStateChanged(livekit::Room& room,
                                                 const livekit::ConnectionStateChangedEvent& event) {
  switch (event.state) {
    case livekit::ConnectionState::Connected:
      transitionToReconnected(room);
      break;
    case livekit::ConnectionState::Reconnecting:
      transitionToReconnecting();
      break;
    case livekit::ConnectionState::Disconnected:
      transitionToDisconnected(livekit::DisconnectReason::Unknown);
      break;
  }
  connection_diagnostics_.onConnectionStateChanged(room, event);
}

void ConnectionManager::onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent& event) {
  transitionToReconnecting();
  connection_diagnostics_.onReconnecting(room, event);
}

void ConnectionManager::onReconnected(livekit::Room& room, const livekit::ReconnectedEvent& event) {
  transitionToReconnected(room);
  connection_diagnostics_.onReconnected(room, event);
}

void ConnectionManager::onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) {
  transitionToDisconnected(event.reason);
  connection_diagnostics_.onDisconnected(room, event);
}

void ConnectionManager::transitionToReconnecting() {
  State expected = State::Connected;
  if (state_.compare_exchange_strong(expected, State::Reconnecting)) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost; SDK reconnecting");
    disableOperations();
  }
}

void ConnectionManager::transitionToReconnected(livekit::Room& room) {
  State expected = State::Reconnecting;
  if (state_.compare_exchange_strong(expected, State::Connected)) {
    markConnectionGained(room);
    enableOperations();
  }
}

void ConnectionManager::transitionToDisconnected(livekit::DisconnectReason reason) {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Disconnected && previous != State::WaitingForRoomEos &&
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
    closeSession();
  }
}

void ConnectionManager::onRoomEos() {
  State previous = state_.load();
  while (previous != State::Stopped && previous != State::Disconnected &&
         !state_.compare_exchange_weak(previous, State::Disconnected)) {
  }

  if (previous == State::Connected) {
    RCLCPP_WARN(logger_, "LiveKit room connection lost; room event stream ended");
  }
  if (previous != State::Stopped && previous != State::Disconnected && previous != State::WaitingForRoomEos) {
    closeSession();
    connection_diagnostics_.markDisconnected();
  }
}

void ConnectionManager::onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event) {
  connection_diagnostics_.onParticipantConnected(room, event);
}

void ConnectionManager::onParticipantDisconnected(livekit::Room& room,
                                                  const livekit::ParticipantDisconnectedEvent& event) {
  connection_diagnostics_.onParticipantDisconnected(room, event);
}

void ConnectionManager::onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent& event) {
  connection_diagnostics_.onRoomUpdated(room, event);
}

void ConnectionManager::onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent& event) {
  connection_diagnostics_.onParticipantsUpdated(room, event);
}

void ConnectionManager::stop() {
  state_.store(State::Stopped);
  closeSession();
}

void ConnectionManager::beginSessionAttempt() {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  operations_enabled_->store(false);
  // A latched pause keeps the barrier closed for the whole join attempt, so
  // waiters drop work instead of blocking until try_connect returns.
  session_closed_ = forwarding_paused_;
}

void ConnectionManager::enableOperations() {
  {
    const std::lock_guard<std::mutex> lock(session_mutex_);
    if (forwarding_paused_) {
      // A latched pause outlives connection transitions. Keep the barrier
      // closed so waiters drop work rather than block on a session that will
      // not enable operations.
      operations_enabled_->store(false);
      session_closed_ = true;
    } else {
      session_closed_ = false;
      operations_enabled_->store(true);
    }
  }
  session_cv_.notify_all();
}

void ConnectionManager::disableOperations() {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  operations_enabled_->store(false);
  // Same reasoning as beginSessionAttempt: an in-session reconnect must not
  // reopen the barrier while operations are paused.
  session_closed_ = forwarding_paused_;
}

void ConnectionManager::closeSession() {
  {
    const std::lock_guard<std::mutex> lock(session_mutex_);
    operations_enabled_->store(false);
    session_closed_ = true;
  }
  session_cv_.notify_all();
}

void ConnectionManager::markConnectionGained(livekit::Room& room) {
  const auto local_participant = room.localParticipant().lock();
  const std::string identity = local_participant ? local_participant->identity() : "unknown";
  const std::string room_name = room.roomInfo().name;
  if (has_connected_.exchange(true)) {
    RCLCPP_INFO(logger_, "LiveKit room connection restored to '%s' with identity '%s'", room_name.c_str(),
                identity.c_str());
  } else {
    RCLCPP_INFO(logger_, "Connected to LiveKit room '%s' with identity '%s'", room_name.c_str(), identity.c_str());
  }
}

} // namespace ros_portal
