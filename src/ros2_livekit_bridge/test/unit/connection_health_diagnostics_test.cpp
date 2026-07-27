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

#include <gtest/gtest.h>

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <utility>

#include "ros2_livekit_bridge/diagnostics/connection_health.hpp"
#include "ros2_livekit_bridge/diagnostics/manager.hpp"
#include "diagnostics_test_utils.hpp"

namespace ros2_livekit_bridge::diagnostics {
namespace {

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

livekit::RtcStats makeCandidatePairStats(std::uint64_t bytes_sent = 1200, std::uint64_t bytes_received = 1300,
                                         std::int64_t timestamp_ms = 1000) {
  livekit::RtcCandidatePairStats candidate_pair;
  candidate_pair.rtc.id = "candidate-pair-stat";
  candidate_pair.rtc.timestamp_ms = timestamp_ms;
  candidate_pair.candidate_pair.transport_id = "transport-1";
  candidate_pair.candidate_pair.local_candidate_id = "local-1";
  candidate_pair.candidate_pair.remote_candidate_id = "remote-1";
  candidate_pair.candidate_pair.state = livekit::IceCandidatePairState::Succeeded;
  candidate_pair.candidate_pair.nominated = true;
  candidate_pair.candidate_pair.packets_sent = 10;
  candidate_pair.candidate_pair.packets_received = 11;
  candidate_pair.candidate_pair.bytes_sent = bytes_sent;
  candidate_pair.candidate_pair.bytes_received = bytes_received;
  candidate_pair.candidate_pair.current_round_trip_time = 0.012;
  candidate_pair.candidate_pair.available_outgoing_bitrate = 45000.0;
  candidate_pair.candidate_pair.available_incoming_bitrate = 55000.0;

  livekit::RtcStats stats;
  stats.stats = candidate_pair;
  return stats;
}

livekit::RtcStats makeFallbackCandidatePairStats() {
  livekit::RtcCandidatePairStats candidate_pair;
  candidate_pair.rtc.id = "fallback-candidate-pair-stat";
  candidate_pair.rtc.timestamp_ms = 1000;
  candidate_pair.candidate_pair.state = livekit::IceCandidatePairState::Succeeded;
  candidate_pair.candidate_pair.nominated = true;
  candidate_pair.candidate_pair.bytes_sent = 5000;
  candidate_pair.candidate_pair.bytes_received = 6000;
  candidate_pair.candidate_pair.current_round_trip_time = 0.020;

  livekit::RtcStats stats;
  stats.stats = candidate_pair;
  return stats;
}

livekit::RtcStats makeTransportStats(std::string selected_candidate_pair_id) {
  livekit::RtcTransportStats transport;
  transport.rtc.id = "transport-stat";
  transport.rtc.timestamp_ms = 1000;
  transport.transport.packets_sent = 20;
  transport.transport.packets_received = 21;
  transport.transport.bytes_sent = 2200;
  transport.transport.bytes_received = 2300;
  transport.transport.ice_role = livekit::IceRole::Controlling;
  transport.transport.dtls_state = livekit::DtlsTransportState::Connected;
  transport.transport.ice_state = livekit::IceTransportState::Completed;
  transport.transport.selected_candidate_pair_id = std::move(selected_candidate_pair_id);
  transport.transport.dtls_role = livekit::DtlsRole::Client;
  transport.transport.selected_candidate_pair_changes = 2;

  livekit::RtcStats stats;
  stats.stats = transport;
  return stats;
}

livekit::RtcStats makeInboundRtpStats(std::int64_t packets_lost, double jitter_seconds) {
  livekit::RtcInboundRtpStats inbound;
  inbound.rtc.id = "inbound-stat";
  inbound.rtc.timestamp_ms = 1000;
  inbound.received.packets_lost = packets_lost;
  inbound.received.jitter = jitter_seconds;

  livekit::RtcStats stats;
  stats.stats = inbound;
  return stats;
}

livekit::RtcStats makeDataChannelStats(std::optional<livekit::DataChannelState> state) {
  livekit::RtcDataChannelStats data_channel;
  data_channel.rtc.id = "data-channel-stat";
  data_channel.rtc.timestamp_ms = 1000;
  data_channel.dc.label = "ros-data";
  data_channel.dc.protocol = "binary";
  data_channel.dc.data_channel_identifier = 4;
  data_channel.dc.state = state;
  data_channel.dc.messages_sent = 30;
  data_channel.dc.bytes_sent = 3000;
  data_channel.dc.messages_received = 31;
  data_channel.dc.bytes_received = 3100;

  livekit::RtcStats stats;
  stats.stats = data_channel;
  return stats;
}

livekit::SessionStats makeSessionStats(std::uint64_t bytes_sent = 1200, std::uint64_t bytes_received = 1300,
                                       std::int64_t timestamp_ms = 1000) {
  livekit::SessionStats stats;
  stats.publisher_stats = {makeCandidatePairStats(bytes_sent, bytes_received, timestamp_ms),
                           makeFallbackCandidatePairStats(), makeTransportStats("candidate-pair-stat"),
                           makeDataChannelStats(livekit::DataChannelState::Open),
                           makeDataChannelStats(livekit::DataChannelState::Closed)};
  stats.subscriber_stats = {makeInboundRtpStats(2, 0.007), makeInboundRtpStats(3, 0.011)};
  return stats;
}

TEST(ConnectionHealthDiagnosticsTest, RegistersAndRemovesTaskWithSharedHub) {
  auto node = std::make_shared<rclcpp::Node>("connection_health_diagnostics_unit_test");
  const auto diagnostics = std::make_shared<DiagnosticsManager>(node);
  const auto fns = test::makeDiagnosticsManagerFns(diagnostics);

  {
    ConnectionHealthDiagnostics connection_health(fns);
    EXPECT_EQ(connection_health.snapshot().kind, ConnectionHealthStateKind::Disconnected);
  }
}

TEST(ConnectionHealthDiagnosticsTest, RejectsMissingDiagnostics) {
  EXPECT_THROW(ConnectionHealthDiagnostics({}), std::invalid_argument);
}

TEST(ConnectionHealthDiagnosticsTest, ConnectedStateEmitsOkStatus) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Connected;
  state.room_name = "diagnostics_room";
  state.num_peers = 3;
  state.reconnect_count = 1;
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(status.message, "Connected to LiveKit room");
  EXPECT_EQ(valueFor(status, "connected"), "true");
  EXPECT_EQ(valueFor(status, "state"), "connected");
  EXPECT_EQ(valueFor(status, "num_peers"), "3");
  EXPECT_EQ(valueFor(status, "reconnect_count"), "1");
  EXPECT_EQ(valueFor(status, "room_name"), "diagnostics_room");
  EXPECT_EQ(valueFor(status, "rtc.publisher.0.type"), std::nullopt);
  EXPECT_EQ(valueFor(status, "rtc.stats_available"), "false");
  EXPECT_EQ(valueFor(status, "rtc.transport.ice_state"), "unset");
}

TEST(ConnectionHealthDiagnosticsTest, ReconnectingStateEmitsWarnStatus) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Reconnecting;
  state.room_name = "diagnostics_room";
  state.num_peers = 2;
  state.reconnect_count = 2;
  state.rtc_summary.stats_available = true;
  state.rtc_summary.bytes_sent = 1200;
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(status.message, "Reconnecting to LiveKit room");
  EXPECT_EQ(valueFor(status, "connected"), "false");
  EXPECT_EQ(valueFor(status, "state"), "reconnecting");
  EXPECT_EQ(valueFor(status, "num_peers"), "2");
  EXPECT_EQ(valueFor(status, "reconnect_count"), "2");
  EXPECT_EQ(valueFor(status, "rtc.stats_available"), std::nullopt);
  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_sent"), std::nullopt);
}

TEST(ConnectionHealthDiagnosticsTest, DisconnectedStateEmitsErrorStatus) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Disconnected;
  state.room_name = "diagnostics_room";
  state.num_peers = 3;
  state.rtc_summary.stats_available = true;
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(status.message, "Disconnected from LiveKit room");
  EXPECT_EQ(valueFor(status, "connected"), "false");
  EXPECT_EQ(valueFor(status, "state"), "disconnected");
  EXPECT_EQ(valueFor(status, "rtc.stats_available"), std::nullopt);
}

TEST(ConnectionHealthDiagnosticsTest, EmitsCompactRtcSummary) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Connected;
  state.room_name = "diagnostics_room";
  updateConnectionHealthStatsSnapshot(state, makeSessionStats());
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(valueFor(status, "rtc.stats_available"), "true");
  EXPECT_EQ(valueFor(status, "rtc.transport.ice_state"), "completed");
  EXPECT_EQ(valueFor(status, "rtc.transport.dtls_state"), "connected");
  EXPECT_EQ(valueFor(status, "rtc.transport.candidate_pair_state"), "succeeded");
  EXPECT_EQ(valueFor(status, "rtc.transport.current_round_trip_time_ms"), "12");
  EXPECT_EQ(valueFor(status, "rtc.transport.available_outgoing_bitrate_bps"), "45000");
  EXPECT_EQ(valueFor(status, "rtc.transport.available_incoming_bitrate_bps"), "55000");
  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_sent"), "1200");
  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_received"), "1300");
  EXPECT_EQ(valueFor(status, "rtc.traffic.packets_lost"), "5");
  EXPECT_EQ(valueFor(status, "rtc.traffic.max_jitter_ms"), "11");
  EXPECT_EQ(valueFor(status, "rtc.data_channels.open"), "1");
  EXPECT_EQ(valueFor(status, "rtc.data_channels.total"), "2");

  EXPECT_EQ(valueFor(status, "rtc.publisher.0.type"), std::nullopt);
  EXPECT_EQ(valueFor(status, "rtc.publisher.0.certificate.base64_certificate"), std::nullopt);
}

TEST(ConnectionHealthDiagnosticsTest, ComputesBitratesFromSuccessiveSnapshots) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Connected;
  state.room_name = "diagnostics_room";
  updateConnectionHealthStatsSnapshot(state, makeSessionStats(1000, 2000, 1000));
  updateConnectionHealthStatsSnapshot(state, makeSessionStats(2000, 3000, 2000));
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(valueFor(status, "rtc.traffic.send_bitrate_bps"), "8000");
  EXPECT_EQ(valueFor(status, "rtc.traffic.receive_bitrate_bps"), "8000");
}

TEST(ConnectionHealthDiagnosticsTest, UsesFallbackNominatedCandidatePair) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Connected;
  state.room_name = "diagnostics_room";
  livekit::SessionStats stats;
  stats.publisher_stats = {makeCandidatePairStats(), makeFallbackCandidatePairStats(), makeTransportStats("")};
  updateConnectionHealthStatsSnapshot(state, stats);
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_sent"), "5000");
  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_received"), "6000");
  EXPECT_EQ(valueFor(status, "rtc.transport.current_round_trip_time_ms"), "20");
}

TEST(ConnectionHealthDiagnosticsTest, UnavailableRtcValuesSerializeAsUnset) {
  ConnectionHealthState state;
  state.kind = ConnectionHealthStateKind::Connected;
  state.room_name = "diagnostics_room";
  livekit::SessionStats stats;
  stats.publisher_stats = {makeDataChannelStats(std::nullopt)};
  updateConnectionHealthStatsSnapshot(state, stats);
  diagnostic_updater::DiagnosticStatusWrapper status;

  populateConnectionHealthStatus(state, status);

  EXPECT_EQ(valueFor(status, "rtc.stats_available"), "true");
  EXPECT_EQ(valueFor(status, "rtc.transport.ice_state"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.transport.dtls_state"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.transport.candidate_pair_state"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.transport.current_round_trip_time_ms"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.traffic.bytes_sent"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.traffic.send_bitrate_bps"), "unset");
  EXPECT_EQ(valueFor(status, "rtc.data_channels.open"), "0");
  EXPECT_EQ(valueFor(status, "rtc.data_channels.total"), "1");
}

} // namespace
} // namespace ros2_livekit_bridge::diagnostics
