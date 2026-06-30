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

#include <cstdint>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <livekit/room.h>
#include <livekit/room_delegate.h>
#include <rclcpp/rclcpp.hpp>

#include "ros2_livekit_bridge/types.hpp"

namespace ros2_livekit_bridge
{

namespace diagnostics
{
class ConnectionHealthDiagnostics;
} // namespace diagnostics
class Ros2CliManager;
class TopicForwarder;

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
  explicit Ros2LiveKitBridge(
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~Ros2LiveKitBridge() override;

  /// @brief Initialize bridge configuration, LiveKit connection, and polling.
  /// @return True if initialization completed, false for expected startup
  /// failures that have already been logged.
  bool initialize();

  int ros_threads() const {return ros_threads_;}

private:
  /// @brief Poll the topics and create subscribers for the allowed topics
  void pollTopics();

  /// @brief Poll LiveKit stats used by connection-health diagnostics.
  void pollConnectionStats();

  /// @brief Handle a remote LiveKit data track being published.
  void onDataTrackPublished(
    livekit::Room & room,
    const livekit::DataTrackPublishedEvent & event) override;

  /// @brief Stop republishing a remote LiveKit data track when it is removed.
  void onDataTrackUnpublished(
    livekit::Room & room,
    const livekit::DataTrackUnpublishedEvent & event) override;

  // The LiveKit room exposes a single delegate, so the bridge owns it and
  // forwards connection-health events to the diagnostics helper below.

  /// @brief Forward participant-connected events to connection diagnostics.
  void onParticipantConnected(
    livekit::Room & room,
    const livekit::ParticipantConnectedEvent & event) override;

  /// @brief Forward participant-disconnected events to connection diagnostics.
  void onParticipantDisconnected(
    livekit::Room & room,
    const livekit::ParticipantDisconnectedEvent & event) override;

  /// @brief Forward connection-state changes to connection diagnostics.
  void onConnectionStateChanged(
    livekit::Room & room,
    const livekit::ConnectionStateChangedEvent & event) override;

  /// @brief Forward terminal disconnect events to connection diagnostics.
  void onDisconnected(
    livekit::Room & room,
    const livekit::DisconnectedEvent & event) override;

  /// @brief Forward reconnecting events to connection diagnostics.
  void onReconnecting(
    livekit::Room & room,
    const livekit::ReconnectingEvent & event) override;

  /// @brief Forward reconnected events to connection diagnostics.
  void onReconnected(
    livekit::Room & room,
    const livekit::ReconnectedEvent & event) override;

  /// @brief Forward room-updated events to connection diagnostics.
  void onRoomUpdated(
    livekit::Room & room,
    const livekit::RoomUpdatedEvent & event) override;

  /// @brief Forward participants-updated events to connection diagnostics.
  void onParticipantsUpdated(
    livekit::Room & room,
    const livekit::ParticipantsUpdatedEvent & event) override;

  /// @brief Check whether a remote participant identity is present in the room.
  /// @param participant_id LiveKit participant identity to look up.
  /// @return True when the participant exists in the connected room.
  bool hasParticipant(const std::string & participant_id) const;

  /// @brief Invoke a LiveKit RPC method through the room's local participant.
  /// @param participant_id LiveKit participant identity to call.
  /// @param method LiveKit RPC method name.
  /// @param payload JSON request payload.
  /// @param timeout_sec Response timeout in seconds.
  /// @return JSON response payload returned by the remote participant, or
  /// std::nullopt when the RPC call fails.
  std::optional<std::string> rpcPerform(
    const std::string & participant_id, const std::string & method,
    const std::string & payload, std::uint8_t timeout_sec);

  /// @brief Register a local LiveKit RPC handler on the room's local
  /// participant, adapting the JSON-string handler to the SDK signature.
  /// @param method LiveKit RPC method name.
  /// @param handler Callback that receives and returns JSON strings.
  /// @return True on success, false when the local participant is unavailable.
  bool rpcRegisterMethod(
    const std::string & method, RpcHandler handler);

  /// @brief Unregister a local LiveKit RPC handler from the room's local
  /// participant.
  /// @param method LiveKit RPC method name.
  /// @return True on success, false when the local participant is unavailable.
  bool rpcUnregisterMethod(const std::string & method);

  /// @brief Create TopicForwarder after LiveKit room connection succeeds.
  /// @param outgoing_topic_compiled_patterns Compiled ROS-to-LiveKit topic
  /// patterns.
  /// @param incoming_topic_compiled_patterns Compiled LiveKit-to-ROS topic
  /// patterns.
  /// @return True on success, false when the topic forwarder could not be initialized.
  bool initializeTopicForwarder(
    std::vector<std::regex> outgoing_topic_compiled_patterns,
    std::vector<std::regex> incoming_topic_compiled_patterns);

  /// @brief Create Ros2CliManager after LiveKit room connection succeeds.
  /// @return True on success, false when the ROS2 CLI manager could not be initialized.
  bool initializeRos2CliManager();

  //! @brief The name of the room
  std::string room_name_;
  //! @brief The period for polling the topics
  int topic_polling_period_ms_;

  //! @brief The minimum QoS depth
  size_t min_qos_depth_;
  //! @brief The maximum QoS depth
  size_t max_qos_depth_;
  //! @brief The patterns for the topics that should be forced to BEST_EFFORT
  std::vector<std::regex> best_effort_qos_topic_patterns_;
  //! @brief Number of threads for the MultiThreadedExecutor (0 = use system
  //! default)
  int ros_threads_;
  //! @brief Tracks whether bridge initialization has completed.
  bool initialized_;
  //! @brief Reentrant callback group shared by all subscriptions
  rclcpp::CallbackGroup::SharedPtr reentrant_callback_group_;
  //! @brief The timer for the polling for new topics
  rclcpp::TimerBase::SharedPtr poll_timer_;

  //! @brief LiveKit room connection for publishing tracks directly via the SDK.
  std::unique_ptr<livekit::Room> room_;
  //! @brief Topic forwarding component for ROS-to-LiveKit and LiveKit-to-ROS.
  std::unique_ptr<TopicForwarder> topic_forwarder_;
  //! @brief ROS CLI service/RPC manager for remote graph introspection.
  std::unique_ptr<Ros2CliManager> ros2_cli_manager_;
  //! @brief LiveKit connection health diagnostics publisher.
  std::unique_ptr<diagnostics::ConnectionHealthDiagnostics>
  connection_diagnostics_;
  //! @brief Timer for best-effort LiveKit stats polling.
  rclcpp::TimerBase::SharedPtr connection_stats_timer_;
};

} // namespace ros2_livekit_bridge
