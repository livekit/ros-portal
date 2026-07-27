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

#include <livekit/room.h>
#include <livekit/room_delegate.h>

#include <cstdint>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ros2_livekit_bridge/diagnostics/diagnostics_fns.hpp"
#include "ros2_livekit_bridge/service_forwarder.hpp"
#include "ros2_livekit_bridge/types.hpp"
#include "ros2_livekit_bridge_config/config/config_parser.hpp"

namespace ros2_livekit_bridge {

namespace diagnostics {
class ConnectionHealthDiagnostics;
} // namespace diagnostics
namespace cli {
class Manager;
} // namespace cli
class TopicForwarder;
class LatchedTopicForwarder;

/// @brief The main bridge node for the ROS2 LiveKit bridge.
///
/// This node is responsible for polling the ROS2 topic graph, matching topics
/// against user-defined patterns, and creating subscribers for the allowed
/// topics. The bridge treats video and audio as LK video/audio tracks and other
/// topics as data tracks.
class Ros2LiveKitBridge : public rclcpp::Node, public livekit::RoomDelegate {
public:
  /// @brief Constructor for the ROS2 LiveKit bridge.
  /// @param options The options for the node
  explicit Ros2LiveKitBridge(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~Ros2LiveKitBridge() override;

  /// @brief Initialize bridge configuration, LiveKit connection, and polling.
  /// @return True if initialization completed, false for expected startup
  /// failures that have already been logged.
  bool initialize();

  /// @brief Disconnect LiveKit and release bridge resources.
  ///
  /// Call this before releasing the final shared owner. The LiveKit SDK must
  /// not disconnect a room from one of its delegate callbacks.
  void shutdown();

  int ros_threads() const { return ros_threads_; }

  /// @brief Check whether a remote participant identity is present in the room.
  /// @param participant_id LiveKit participant identity to look up.
  /// @return True when the participant exists in the connected room.
  ///
  /// Participant presence is populated asynchronously as the room receives
  /// connect/disconnect signaling, so callers that need a peer to be reachable
  /// should poll this until it returns true rather than assuming immediacy.
  bool hasParticipant(const std::string& participant_id) const;

private:
  /// @brief Poll the topics and create subscribers for the allowed topics
  void pollTopics();

  /// @brief Poll LiveKit stats used by connection-health diagnostics.
  void pollConnectionStats();

  /// @brief Handle a remote LiveKit data track being published.
  void onDataTrackPublished(livekit::Room& room, const livekit::DataTrackPublishedEvent& event) override;

  /// @brief Stop republishing a remote LiveKit data track when it is removed.
  void onDataTrackUnpublished(livekit::Room& room, const livekit::DataTrackUnpublishedEvent& event) override;

  // The LiveKit room exposes a single delegate, so the bridge owns it and
  // forwards connection-health events to the diagnostics helper below.

  /// @brief Forward participant-connected events to connection diagnostics.
  void onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event) override;

  /// @brief Forward participant-disconnected events to connection diagnostics.
  void onParticipantDisconnected(livekit::Room& room, const livekit::ParticipantDisconnectedEvent& event) override;

  /// @brief Forward connection-state changes to connection diagnostics.
  void onConnectionStateChanged(livekit::Room& room, const livekit::ConnectionStateChangedEvent& event) override;

  /// @brief Forward terminal disconnect events to connection diagnostics.
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) override;

  /// @brief Forward reconnecting events to connection diagnostics.
  void onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent& event) override;

  /// @brief Forward reconnected events to connection diagnostics.
  void onReconnected(livekit::Room& room, const livekit::ReconnectedEvent& event) override;

  /// @brief Forward room-updated events to connection diagnostics.
  void onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent& event) override;

  /// @brief Forward participants-updated events to connection diagnostics.
  void onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent& event) override;

  /// @brief Invoke a LiveKit RPC method through the room's local participant.
  /// @param participant_id LiveKit participant identity to call.
  /// @param method LiveKit RPC method name.
  /// @param payload JSON request payload.
  /// @param timeout_sec Response timeout in seconds.
  /// @return JSON response payload returned by the remote participant, or
  /// std::nullopt when the RPC call fails.
  std::optional<std::string> rpcPerform(const std::string& participant_id, const std::string& method,
                                        const std::string& payload, std::uint8_t timeout_sec);

  /// @brief Register a local LiveKit RPC handler on the room's local
  /// participant, adapting the JSON-string handler to the SDK signature.
  /// @param method LiveKit RPC method name.
  /// @param handler Callback that receives and returns JSON strings.
  /// @return True on success, false when the local participant is unavailable.
  bool rpcRegisterMethod(const std::string& method, RpcHandler handler);

  /// @brief Unregister a local LiveKit RPC handler from the room's local
  /// participant.
  /// @param method LiveKit RPC method name.
  /// @return True on success, false when the local participant is unavailable.
  bool rpcUnregisterMethod(const std::string& method);

  /// @brief Create TopicForwarder after LiveKit room connection succeeds.
  /// @param topics Configured topics; mapped to TopicForwarder options via
  /// config_mapping (QoS bounds and best-effort patterns come from ROS params).
  /// @return True on success, false when the topic forwarder could not be initialized.
  bool initializeTopicForwarder(const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics);

  /// @brief Create Manager after LiveKit room connection succeeds.
  /// @return True on success, false when the ROS2 CLI manager could not be initialized.
  bool initializeCliManager();

  /// @brief Build the diagnostics registration functions handed to components.
  ///
  /// The returned wrappers close over this node and validate `diagnostics_updater_`
  /// before forwarding, logging FATAL if a component registers or deregisters
  /// a task while the shared updater does not exist.
  diagnostics::DiagnosticsManagerFns makeDiagnosticsFns();

  /// @brief Create ServiceForwarder after LiveKit room connection succeeds.
  /// @param services Configured services; outbound routes are derived here.
  /// @return True on success, false when the service forwarder could not be initialized.
  bool initializeServiceForwarder(const std::vector<ros2_livekit_bridge_config::ServiceConfig>& services);

  /// @brief Create LatchedTopicForwarder after LiveKit room connection succeeds.
  ///
  /// Handles topics flagged `latched` (e.g. /tf_static) over a reliable RPC
  /// push-with-ack instead of DataTracks. No-op when no latched topics are
  /// configured.
  /// @param topics Configured topics; latched outbound/inbound sets are derived
  /// here.
  /// @return True on success (including when there is nothing to do), false when
  /// the forwarder could not be initialized.
  bool initializeLatchedTopicForwarder(const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics);

  //! @brief The period for polling the topics
  int topic_polling_period_ms_;

  //! @brief The minimum QoS depth
  size_t min_qos_depth_;
  //! @brief The maximum QoS depth
  size_t max_qos_depth_;
  //! @brief Number of threads for the MultiThreadedExecutor (0 = use system
  //! default)
  int ros_threads_;
  //! @brief Tracks whether bridge initialization has completed.
  bool initialized_;
  //! @brief Serializes explicit shutdown with the destructor fallback.
  std::mutex shutdown_mutex_;
  //! @brief Quiesces data-track publication callbacks before room disconnect.
  std::mutex data_track_callback_mutex_;
  //! @brief Prevents snapshotted publication callbacks from entering during shutdown.
  bool shutting_down_;
  //! @brief Reentrant callback group shared by all subscriptions
  rclcpp::CallbackGroup::SharedPtr reentrant_callback_group_;
  //! @brief The timer for the polling for new topics
  rclcpp::TimerBase::SharedPtr poll_timer_;

  //! @brief LiveKit room connection for publishing tracks directly via the SDK.
  std::unique_ptr<livekit::Room> room_;
  //! @brief Shared diagnostics updater for all bridge diagnostic tasks.
  std::unique_ptr<diagnostic_updater::Updater> diagnostics_updater_;
  //! @brief Topic forwarding component for ROS-to-LiveKit and LiveKit-to-ROS.
  std::unique_ptr<TopicForwarder> topic_forwarder_;
  //! @brief Latched-topic (e.g. /tf_static) forwarding over LiveKit RPC.
  std::unique_ptr<LatchedTopicForwarder> latched_topic_forwarder_;
  //! @brief ROS CLI service/RPC manager for remote graph introspection.
  std::unique_ptr<cli::Manager> cli_manager_;
  //! @brief ROS service forwarding component for local proxy services.
  std::unique_ptr<ServiceForwarder> service_forwarder_;
  //! @brief LiveKit connection health diagnostic task owner.
  std::unique_ptr<diagnostics::ConnectionHealthDiagnostics> connection_diagnostics_;
  //! @brief Timer for best-effort LiveKit stats polling.
  rclcpp::TimerBase::SharedPtr connection_stats_timer_;
};

} // namespace ros2_livekit_bridge
