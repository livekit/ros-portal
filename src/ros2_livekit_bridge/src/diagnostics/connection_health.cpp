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

#include "ros2_livekit_bridge/diagnostics/connection_health.hpp"

#include <livekit/room.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ros2_livekit_bridge::diagnostics {

namespace {

constexpr double kDefaultDiagnosticPeriodSec = 1.0;
constexpr double kSecondsToMilliseconds = 1000.0;
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr double kBitsPerByte = 8.0;

struct CandidatePairSnapshot {
  std::string id;
  std::int64_t timestamp_ms{0};
  livekit::CandidatePairStats stats;
};

struct TransportSnapshot {
  livekit::TransportStats stats;
};

struct RtcStatsAccumulator {
  std::vector<CandidatePairSnapshot> candidate_pairs;
  std::vector<TransportSnapshot> transports;
  std::int64_t packets_lost{0};
  std::optional<double> max_jitter_ms;
  std::uint32_t data_channels_open{0};
  std::uint32_t data_channels_total{0};
};

std::string valueToString(bool value) { return value ? "true" : "false"; }

std::string valueToString(livekit::DtlsTransportState value) {
  switch (value) {
    case livekit::DtlsTransportState::New:
      return "new";
    case livekit::DtlsTransportState::Connecting:
      return "connecting";
    case livekit::DtlsTransportState::Connected:
      return "connected";
    case livekit::DtlsTransportState::Closed:
      return "closed";
    case livekit::DtlsTransportState::Failed:
      return "failed";
    case livekit::DtlsTransportState::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string valueToString(livekit::IceTransportState value) {
  switch (value) {
    case livekit::IceTransportState::New:
      return "new";
    case livekit::IceTransportState::Checking:
      return "checking";
    case livekit::IceTransportState::Connected:
      return "connected";
    case livekit::IceTransportState::Completed:
      return "completed";
    case livekit::IceTransportState::Disconnected:
      return "disconnected";
    case livekit::IceTransportState::Failed:
      return "failed";
    case livekit::IceTransportState::Closed:
      return "closed";
    case livekit::IceTransportState::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string valueToString(livekit::IceCandidatePairState value) {
  switch (value) {
    case livekit::IceCandidatePairState::Frozen:
      return "frozen";
    case livekit::IceCandidatePairState::Waiting:
      return "waiting";
    case livekit::IceCandidatePairState::InProgress:
      return "in_progress";
    case livekit::IceCandidatePairState::Failed:
      return "failed";
    case livekit::IceCandidatePairState::Succeeded:
      return "succeeded";
    case livekit::IceCandidatePairState::Unknown:
      return "unknown";
  }
  return "unknown";
}

template <typename T>
std::string valueToString(const T& value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

void addField(diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key, const std::string& value) {
  status.add(key, value);
}

template <typename T>
void addField(diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key, const T& value) {
  status.add(key, valueToString(value));
}

template <typename T>
void addOptionalField(diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key,
                      const std::optional<T>& value) {
  addField(status, key, value.has_value() ? valueToString(*value) : "unset");
}

const char* stateToString(ConnectionHealthStateKind state) {
  switch (state) {
    case ConnectionHealthStateKind::Connected:
      return "connected";
    case ConnectionHealthStateKind::Reconnecting:
      return "reconnecting";
    case ConnectionHealthStateKind::Disconnected:
      return "disconnected";
  }
  return "disconnected";
}

std::uint64_t totalBytes(const livekit::CandidatePairStats& stats) { return stats.bytes_sent + stats.bytes_received; }

void updateMaxJitter(RtcStatsAccumulator& accumulator, double jitter_seconds) {
  const double jitter_ms = jitter_seconds * kSecondsToMilliseconds;
  if (!accumulator.max_jitter_ms.has_value() || jitter_ms > *accumulator.max_jitter_ms) {
    accumulator.max_jitter_ms = jitter_ms;
  }
}

void accumulateRtcStats(RtcStatsAccumulator& accumulator, const livekit::RtcStats& stats) {
  std::visit(
      [&accumulator](const auto& typed_stats) {
        using StatsType = std::decay_t<decltype(typed_stats)>;

        if constexpr (std::is_same_v<StatsType, livekit::RtcTransportStats>) {
          accumulator.transports.push_back({typed_stats.transport});
        } else if constexpr (std::is_same_v<StatsType, livekit::RtcCandidatePairStats>) {
          accumulator.candidate_pairs.push_back(
              {typed_stats.rtc.id, typed_stats.rtc.timestamp_ms, typed_stats.candidate_pair});
        } else if constexpr (std::is_same_v<StatsType, livekit::RtcInboundRtpStats>) {
          accumulator.packets_lost += typed_stats.received.packets_lost;
          updateMaxJitter(accumulator, typed_stats.received.jitter);
        } else if constexpr (std::is_same_v<StatsType, livekit::RtcRemoteInboundRtpStats>) {
          accumulator.packets_lost += typed_stats.received.packets_lost;
          updateMaxJitter(accumulator, typed_stats.received.jitter);
        } else if constexpr (std::is_same_v<StatsType, livekit::RtcDataChannelStats>) {
          ++accumulator.data_channels_total;
          if (typed_stats.dc.state == livekit::DataChannelState::Open) {
            ++accumulator.data_channels_open;
          }
        }
      },
      stats.stats);
}

RtcStatsAccumulator summarizeRawStats(const livekit::SessionStats& stats) {
  RtcStatsAccumulator accumulator;
  for (const auto& rtc_stats : stats.publisher_stats) {
    accumulateRtcStats(accumulator, rtc_stats);
  }
  for (const auto& rtc_stats : stats.subscriber_stats) {
    accumulateRtcStats(accumulator, rtc_stats);
  }
  return accumulator;
}

const TransportSnapshot* selectTransport(const RtcStatsAccumulator& accumulator) {
  const auto selected_transport =
      std::find_if(accumulator.transports.begin(), accumulator.transports.end(),
                   [](const auto& transport) { return !transport.stats.selected_candidate_pair_id.empty(); });

  if (selected_transport != accumulator.transports.end()) {
    return &*selected_transport;
  }

  if (!accumulator.transports.empty()) {
    return &accumulator.transports.front();
  }

  return nullptr;
}

const CandidatePairSnapshot* findCandidatePairById(const RtcStatsAccumulator& accumulator, const std::string& id) {
  const auto selected_pair = std::find_if(accumulator.candidate_pairs.begin(), accumulator.candidate_pairs.end(),
                                          [&id](const auto& candidate_pair) { return candidate_pair.id == id; });

  if (selected_pair == accumulator.candidate_pairs.end()) {
    return nullptr;
  }
  return &*selected_pair;
}

const CandidatePairSnapshot* selectCandidatePair(const RtcStatsAccumulator& accumulator,
                                                 const TransportSnapshot* transport) {
  if (transport && !transport->stats.selected_candidate_pair_id.empty()) {
    if (const auto* selected_pair = findCandidatePairById(accumulator, transport->stats.selected_candidate_pair_id)) {
      return selected_pair;
    }
  }

  const CandidatePairSnapshot* best_pair = nullptr;
  for (const auto& candidate_pair : accumulator.candidate_pairs) {
    const auto& stats = candidate_pair.stats;
    if (!stats.nominated || stats.state != livekit::IceCandidatePairState::Succeeded) {
      continue;
    }

    if (!best_pair || totalBytes(stats) > totalBytes(best_pair->stats)) {
      best_pair = &candidate_pair;
    }
  }

  return best_pair;
}

std::optional<double> computeBitrate(std::uint64_t previous_bytes, std::uint64_t current_bytes,
                                     std::int64_t elapsed_ms) {
  if (elapsed_ms <= 0 || current_bytes < previous_bytes) {
    return std::nullopt;
  }

  return static_cast<double>(current_bytes - previous_bytes) * kBitsPerByte * kMillisecondsPerSecond /
         static_cast<double>(elapsed_ms);
}

void updateBitrates(ConnectionHealthState& state, const CandidatePairSnapshot& candidate_pair) {
  auto& summary = state.rtc_summary;
  if (!summary.bytes_sent.has_value() || !summary.bytes_received.has_value()) {
    state.previous_traffic.reset();
    return;
  }

  if (state.previous_traffic.has_value() && state.previous_traffic->candidate_pair_id == candidate_pair.id) {
    const auto elapsed_ms = candidate_pair.timestamp_ms - state.previous_traffic->timestamp_ms;
    summary.send_bitrate_bps = computeBitrate(state.previous_traffic->bytes_sent, *summary.bytes_sent, elapsed_ms);
    summary.receive_bitrate_bps =
        computeBitrate(state.previous_traffic->bytes_received, *summary.bytes_received, elapsed_ms);
  }

  state.previous_traffic = ConnectionHealthRtcTrafficCounters{candidate_pair.id, candidate_pair.timestamp_ms,
                                                              *summary.bytes_sent, *summary.bytes_received};
}

ConnectionHealthRtcSummary makeRtcSummary(ConnectionHealthState& state, const livekit::SessionStats& stats) {
  auto accumulator = summarizeRawStats(stats);
  ConnectionHealthRtcSummary summary;
  summary.stats_available = true;
  summary.packets_lost = accumulator.packets_lost;
  summary.max_jitter_ms = accumulator.max_jitter_ms;
  summary.data_channels_open = accumulator.data_channels_open;
  summary.data_channels_total = accumulator.data_channels_total;

  const auto* transport = selectTransport(accumulator);
  if (transport) {
    if (transport->stats.ice_state.has_value()) {
      summary.ice_state = valueToString(*transport->stats.ice_state);
    }
    if (transport->stats.dtls_state.has_value()) {
      summary.dtls_state = valueToString(*transport->stats.dtls_state);
    }
  }

  const auto* candidate_pair = selectCandidatePair(accumulator, transport);
  if (candidate_pair) {
    if (candidate_pair->stats.state.has_value()) {
      summary.candidate_pair_state = valueToString(*candidate_pair->stats.state);
    }
    summary.current_round_trip_time_ms = candidate_pair->stats.current_round_trip_time * kSecondsToMilliseconds;
    summary.available_outgoing_bitrate_bps = candidate_pair->stats.available_outgoing_bitrate;
    summary.available_incoming_bitrate_bps = candidate_pair->stats.available_incoming_bitrate;
    summary.bytes_sent = candidate_pair->stats.bytes_sent;
    summary.bytes_received = candidate_pair->stats.bytes_received;
  } else {
    state.previous_traffic.reset();
  }

  state.rtc_summary = summary;
  if (candidate_pair) {
    updateBitrates(state, *candidate_pair);
  }

  return state.rtc_summary;
}

void clearRtcSummary(ConnectionHealthState& state) {
  state.rtc_summary = ConnectionHealthRtcSummary{};
  state.previous_traffic.reset();
}

void addRtcSummaryFields(diagnostic_updater::DiagnosticStatusWrapper& status,
                         const ConnectionHealthRtcSummary& summary) {
  addField(status, "rtc.stats_available", summary.stats_available);
  addOptionalField(status, "rtc.transport.ice_state", summary.ice_state);
  addOptionalField(status, "rtc.transport.dtls_state", summary.dtls_state);
  addOptionalField(status, "rtc.transport.candidate_pair_state", summary.candidate_pair_state);
  addOptionalField(status, "rtc.transport.current_round_trip_time_ms", summary.current_round_trip_time_ms);
  addOptionalField(status, "rtc.transport.available_outgoing_bitrate_bps", summary.available_outgoing_bitrate_bps);
  addOptionalField(status, "rtc.transport.available_incoming_bitrate_bps", summary.available_incoming_bitrate_bps);
  addOptionalField(status, "rtc.traffic.bytes_sent", summary.bytes_sent);
  addOptionalField(status, "rtc.traffic.bytes_received", summary.bytes_received);
  addOptionalField(status, "rtc.traffic.send_bitrate_bps", summary.send_bitrate_bps);
  addOptionalField(status, "rtc.traffic.receive_bitrate_bps", summary.receive_bitrate_bps);
  addField(status, "rtc.traffic.packets_lost", summary.packets_lost);
  addOptionalField(status, "rtc.traffic.max_jitter_ms", summary.max_jitter_ms);
  addField(status, "rtc.data_channels.open", summary.data_channels_open);
  addField(status, "rtc.data_channels.total", summary.data_channels_total);
}

} // namespace

void updateConnectionHealthStatsSnapshot(ConnectionHealthState& state, const livekit::SessionStats& stats) {
  makeRtcSummary(state, stats);
}

void populateConnectionHealthStatus(const ConnectionHealthState& state,
                                    diagnostic_updater::DiagnosticStatusWrapper& status) {
  const bool connected = state.kind == ConnectionHealthStateKind::Connected;

  if (connected) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Connected to LiveKit room");
  } else if (state.kind == ConnectionHealthStateKind::Reconnecting) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Reconnecting to LiveKit room");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Disconnected from LiveKit room");
  }

  status.add("connected", connected ? "true" : "false");
  status.add("state", stateToString(state.kind));
  status.add("num_peers", state.num_peers);
  status.add("reconnect_count", state.reconnect_count);
  status.add("room_name", state.room_name);
  if (connected) {
    addRtcSummaryFields(status, state.rtc_summary);
  }
}

ConnectionHealthDiagnostics::ConnectionHealthDiagnostics(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base_interface,
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock_interface,
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logging_interface,
    rclcpp::node_interfaces::NodeParametersInterface::SharedPtr parameters_interface,
    rclcpp::node_interfaces::NodeTimersInterface::SharedPtr timers_interface,
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_interface)
    : state_{ConnectionHealthStateKind::Disconnected, {}, 0, 0, {}, std::nullopt},
      updater_(std::move(base_interface), std::move(clock_interface), std::move(logging_interface),
               std::move(parameters_interface), std::move(timers_interface), std::move(topics_interface),
               kDefaultDiagnosticPeriodSec) {
  updater_.setHardwareID("ros2_livekit_bridge");
  updater_.add("connection_health", this, &ConnectionHealthDiagnostics::populateStatus);
}

void ConnectionHealthDiagnostics::markConnected(livekit::Room& room) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.kind = ConnectionHealthStateKind::Connected;
    state_.room_name = room.roomInfo().name;
  }
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::markDisconnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.kind = ConnectionHealthStateKind::Disconnected;
  state_.room_name.clear();
  state_.num_peers = 0;
  clearRtcSummary(state_);
  pending_stats_.reset();
}

void ConnectionHealthDiagnostics::pollStats(livekit::Room& room) {
  updateStatsFromReadyFuture();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_stats_.has_value() || state_.kind != ConnectionHealthStateKind::Connected) {
      return;
    }
  }

  try {
    auto stats = room.getStats();
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.kind == ConnectionHealthStateKind::Connected && !pending_stats_.has_value()) {
      pending_stats_ = std::move(stats);
    }
  } catch (const std::exception&) {
    std::lock_guard<std::mutex> lock(mutex_);
    clearRtcSummary(state_);
    pending_stats_.reset();
  }
}

ConnectionHealthState ConnectionHealthDiagnostics::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void ConnectionHealthDiagnostics::onParticipantConnected(livekit::Room& room,
                                                         const livekit::ParticipantConnectedEvent&) {
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::onParticipantDisconnected(livekit::Room& room,
                                                            const livekit::ParticipantDisconnectedEvent&) {
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::onConnectionStateChanged(livekit::Room& room,
                                                           const livekit::ConnectionStateChangedEvent& event) {
  switch (event.state) {
    case livekit::ConnectionState::Connected:
      markConnected(room);
      break;
    case livekit::ConnectionState::Reconnecting:
      markReconnecting(room);
      break;
    case livekit::ConnectionState::Disconnected:
      markDisconnected();
      break;
  }
}

void ConnectionHealthDiagnostics::onDisconnected(livekit::Room&, const livekit::DisconnectedEvent&) {
  markDisconnected();
}

void ConnectionHealthDiagnostics::onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent&) {
  markReconnecting(room);
}

void ConnectionHealthDiagnostics::onReconnected(livekit::Room& room, const livekit::ReconnectedEvent&) {
  markConnected(room);
}

void ConnectionHealthDiagnostics::onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent&) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.room_name = room.roomInfo().name;
  }
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent&) {
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) {
  populateConnectionHealthStatus(snapshot(), status);
}

void ConnectionHealthDiagnostics::markReconnecting(livekit::Room& room) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.kind != ConnectionHealthStateKind::Reconnecting) {
      ++state_.reconnect_count;
    }
    state_.kind = ConnectionHealthStateKind::Reconnecting;
    clearRtcSummary(state_);
    pending_stats_.reset();
  }
  updatePeerCount(room);
}

void ConnectionHealthDiagnostics::updatePeerCount(livekit::Room& room) {
  const auto num_peers = room.remoteParticipants().size();
  std::lock_guard<std::mutex> lock(mutex_);
  state_.num_peers = num_peers;
}

void ConnectionHealthDiagnostics::updateStatsFromReadyFuture() {
  std::optional<std::future<livekit::SessionStats>> ready_stats;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_stats_.has_value()) {
      return;
    }

    if (pending_stats_->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      return;
    }

    ready_stats = std::move(pending_stats_);
    pending_stats_.reset();
  }

  try {
    auto stats = ready_stats->get();
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.kind == ConnectionHealthStateKind::Connected) {
      updateConnectionHealthStatsSnapshot(state_, stats);
    }
  } catch (const std::exception&) {
    std::lock_guard<std::mutex> lock(mutex_);
    clearRtcSummary(state_);
  }
}

} // namespace ros2_livekit_bridge::diagnostics
