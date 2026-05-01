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

#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <string>

#include <diagnostic_updater/diagnostic_updater.hpp>
#include <livekit/room_delegate.h>
#include <livekit/stats.h>
#include <rclcpp/rclcpp.hpp>

namespace ros2_livekit_bridge::diagnostics
{

/// High-level LiveKit connection state used by the connection health diagnostic.
enum class ConnectionHealthStateKind
{
  /// The bridge is not currently connected to the LiveKit room.
  Disconnected,
  /// The bridge is connected to the LiveKit room.
  Connected,
  /// The LiveKit SDK is attempting to restore an interrupted connection.
  Reconnecting,
};

/// Compact RTC transport and traffic summary for operator diagnostics.
struct ConnectionHealthRtcSummary
{
  /// Whether a LiveKit stats snapshot has been summarized.
  bool stats_available{false};

  /// ICE transport state for the selected transport, when reported.
  std::optional<std::string> ice_state;

  /// DTLS transport state for the selected transport, when reported.
  std::optional<std::string> dtls_state;

  /// ICE candidate pair state for the selected pair, when reported.
  std::optional<std::string> candidate_pair_state;

  /// Current selected candidate pair round-trip time in milliseconds.
  std::optional<double> current_round_trip_time_ms;

  /// Available outgoing bitrate reported by the selected candidate pair.
  std::optional<double> available_outgoing_bitrate_bps;

  /// Available incoming bitrate reported by the selected candidate pair.
  std::optional<double> available_incoming_bitrate_bps;

  /// Total bytes sent on the selected candidate pair.
  std::optional<std::uint64_t> bytes_sent;

  /// Total bytes received on the selected candidate pair.
  std::optional<std::uint64_t> bytes_received;

  /// Estimated send bitrate computed from consecutive byte counters.
  std::optional<double> send_bitrate_bps;

  /// Estimated receive bitrate computed from consecutive byte counters.
  std::optional<double> receive_bitrate_bps;

  /// Sum of packet loss reported by inbound RTP stats.
  std::int64_t packets_lost{0};

  /// Maximum inbound RTP jitter in milliseconds.
  std::optional<double> max_jitter_ms;

  /// Number of open WebRTC data channels.
  std::uint32_t data_channels_open{0};

  /// Total number of WebRTC data channels.
  std::uint32_t data_channels_total{0};
};

/// Byte counters from the previous RTC stats snapshot.
struct ConnectionHealthRtcTrafficCounters
{
  /// RTC stats id of the selected candidate pair.
  std::string candidate_pair_id;

  /// RTC timestamp associated with the selected counter snapshot.
  std::int64_t timestamp_ms{0};

  /// Total bytes sent in the previous snapshot.
  std::uint64_t bytes_sent{0};

  /// Total bytes received in the previous snapshot.
  std::uint64_t bytes_received{0};
};

/// Cached state used to render the `connection_health` diagnostic status.
struct ConnectionHealthState
{
  /// Current connection state reported by the bridge or LiveKit SDK.
  ConnectionHealthStateKind kind{ConnectionHealthStateKind::Disconnected};

  /// LiveKit room name associated with this bridge instance.
  std::string room_name;

  /// Number of currently known remote participants.
  std::size_t num_peers{0};

  /// Number of times the SDK has entered a reconnecting state.
  std::uint64_t reconnect_count{0};

  /// Latest compact RTC stats summary rendered by diagnostics.
  ConnectionHealthRtcSummary rtc_summary;

  /// Previous selected traffic counters used to compute bitrate deltas.
  std::optional<ConnectionHealthRtcTrafficCounters> previous_traffic;
};

/// Update cached RTC diagnostics from a LiveKit stats snapshot.
/**
 * This function summarizes the raw WebRTC stats tree into a small fixed
 * diagnostic surface and updates previous counters for bitrate calculation.
 *
 * @param state Mutable connection health state to update.
 * @param stats LiveKit stats snapshot to summarize.
 */
void updateConnectionHealthStatsSnapshot(
  ConnectionHealthState & state,
  const livekit::SessionStats & stats);

/// Populate a ROS diagnostic status from a connection health state snapshot.
/**
 * This pure mapping function is shared by the runtime helper and unit tests. It
 * sets the diagnostic level/message and emits stable key/value fields for the
 * base connection state and compact RTC stats summary.
 *
 * @param state Connection health state to render.
 * @param status Diagnostic status wrapper to populate.
 */
void populateConnectionHealthStatus(
  const ConnectionHealthState & state,
  diagnostic_updater::DiagnosticStatusWrapper & status);

/// Maintains and publishes the LiveKit `connection_health` diagnostic task.
/**
 * The helper is registered as a LiveKit room delegate so SDK callbacks can keep
 * the cached connection state current. Publishing remains owned by
 * `diagnostic_updater`; callbacks only mutate cached state and never publish
 * diagnostics directly from SDK threads.
 */
class ConnectionHealthDiagnostics final : public livekit::RoomDelegate
{
public:
  /// Create the diagnostic updater task for a bridge node and room.
  /**
   * @param node ROS node that owns the diagnostic updater.
   * @param room_name LiveKit room name reported in diagnostic key/value fields.
   */
  ConnectionHealthDiagnostics(rclcpp::Node * node, std::string room_name);

  /// Mark the bridge connected and refresh the remote participant count.
  void markConnected(livekit::Room & room);

  /// Mark the bridge disconnected and clear cached RTC summary.
  void markDisconnected();

  /// Update the diagnostic updater and poll LiveKit stats when appropriate.
  /**
   * This method is intended to be called periodically from the ROS executor. It
   * publishes through `diagnostic_updater` and starts or harvests asynchronous
   * LiveKit stats requests without blocking the caller.
   */
  void pollStats(livekit::Room & room);

  /// Return a thread-safe copy of the current diagnostic state.
  ConnectionHealthState snapshot() const;

  /// Update peer count after a remote participant joins.
  void onParticipantConnected(
    livekit::Room & room,
    const livekit::ParticipantConnectedEvent & event) override;

  /// Update peer count after a remote participant leaves.
  void onParticipantDisconnected(
    livekit::Room & room,
    const livekit::ParticipantDisconnectedEvent & event) override;

  /// Update cached connection state after a LiveKit connection-state change.
  void onConnectionStateChanged(
    livekit::Room & room,
    const livekit::ConnectionStateChangedEvent & event) override;

  /// Mark disconnected when the SDK reports a terminal disconnect event.
  void onDisconnected(
    livekit::Room & room,
    const livekit::DisconnectedEvent & event) override;

  /// Mark reconnecting and increment the reconnect counter.
  void onReconnecting(
    livekit::Room & room,
    const livekit::ReconnectingEvent & event) override;

  /// Mark connected after a successful SDK reconnect.
  void onReconnected(
    livekit::Room & room,
    const livekit::ReconnectedEvent & event) override;

  /// Refresh room-derived diagnostic fields after room metadata changes.
  void onRoomUpdated(
    livekit::Room & room,
    const livekit::RoomUpdatedEvent & event) override;

  /// Refresh peer count after a participant collection update.
  void onParticipantsUpdated(
    livekit::Room & room,
    const livekit::ParticipantsUpdatedEvent & event) override;

private:
  /// Populate one updater status from the current state snapshot.
  void populateStatus(diagnostic_updater::DiagnosticStatusWrapper & status);

  /// Mark reconnecting, increment reconnect count, and clear cached RTC summary.
  void markReconnecting(livekit::Room & room);

  /// Store the current remote participant count from the LiveKit room.
  void updatePeerCount(livekit::Room & room);

  /// Move a completed asynchronous LiveKit stats request into cached state.
  void updateStatsFromReadyFuture();

  /// Guards access to all mutable cached diagnostic state.
  mutable std::mutex mutex_;

  /// Latest connection state and RTC summary rendered by the diagnostic task.
  ConnectionHealthState state_;

  /// ROS diagnostic updater that publishes the `connection_health` task.
  diagnostic_updater::Updater updater_;

  /// In-flight asynchronous LiveKit stats request, if one is outstanding.
  std::optional<std::future<livekit::SessionStats>> pending_stats_;
};

} // namespace ros2_livekit_bridge::diagnostics
