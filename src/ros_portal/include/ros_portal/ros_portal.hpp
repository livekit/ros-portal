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

#include <atomic>
#include <cstdint>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/service_forwarder.hpp"
#include "ros_portal/types.hpp"
#include "ros_portal_config/config/config_parser.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros_portal {

namespace diagnostics {
class BuildInfoDiagnostics;
} // namespace diagnostics
namespace cli {
class Manager;
} // namespace cli
class ConnectionManager;
class TopicForwarder;
class LatchedTopicForwarder;
class VideoSourceManager;

/// @brief LiveKit participant attribute key that marks ROS Portal as a robot.
inline constexpr const char* kRobotParticipantAttribute = "lk.robot";

/// @brief The main ROS Portal node.
///
/// This node is responsible for polling the ROS2 topic graph, matching topics
/// against user-defined patterns, and creating subscribers for the allowed
/// topics. ROS Portal treats video and audio as LK video/audio tracks and other
/// topics as data tracks.
class RosPortal : public rclcpp::Node, public livekit::RoomDelegate {
public:
  /// @brief Constructor for the ROS Portal.
  /// @param options The options for the node
  explicit RosPortal(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~RosPortal() override;

  /// @brief Initialize ROS Portal configuration, LiveKit connection management,
  /// and polling.
  /// @return True if initialization completed, false for expected startup
  /// failures that have already been logged.
  bool initialize();

  /// @brief Disconnect LiveKit and release ROS Portal resources.
  ///
  /// Call this before releasing the final shared owner. The LiveKit SDK must
  /// not disconnect a room from one of its delegate callbacks.
  void shutdown();

  int rosThreads() const { return ros_threads_; }

  /// @brief Check whether a remote participant identity is present in the room.
  /// @param participant_id LiveKit participant identity to look up.
  /// @return True when the participant exists in the connected room.
  ///
  /// Participant presence is populated asynchronously as the room receives
  /// connect/disconnect signaling, so callers that need a peer to be reachable
  /// should poll this until it returns true rather than assuming immediacy.
  bool hasParticipant(const std::string& participant_id) const;

private:
  /// @brief Poll the room connection state and make a scheduled connection
  /// attempt when needed.
  void pollConnection();
#ifdef BUILD_TESTING
  FRIEND_TEST(RosPortalDiagnosticsTest, ReportsPartialInitializationAndEffectiveConfiguration);
  FRIEND_TEST(RosPortalDiagnosticsTest, ReportsHealthyAndOverrunStates);
  FRIEND_TEST(RosPortalDiagnosticsTest, CountsSharedRpcFailures);
#endif

  /// @brief Mutable counters and state published by `ros_portal_status`.
  struct DiagnosticState {
    /// @brief Guards string metadata updated during initialization.
    std::mutex metadata_mutex;
    /// @brief Effective configuration file path, or `unset`.
    std::string config_path{"unset"};
    /// @brief Connected local LiveKit identity, or `unset`.
    std::string local_identity{"unset"};
    /// @brief Effective topic polling period.
    std::atomic<int> topic_polling_period_ms{0};
    /// @brief Whether the connection manager is instantiated.
    std::atomic_bool connection_manager_active{false};
    /// @brief Whether the topic forwarder is instantiated.
    std::atomic_bool topic_forwarder_active{false};
    /// @brief Whether the latched-topic forwarder is instantiated.
    std::atomic_bool latched_topic_forwarder_active{false};
    /// @brief Whether the CLI manager is instantiated.
    std::atomic_bool cli_manager_active{false};
    /// @brief Whether the service forwarder is instantiated.
    std::atomic_bool service_forwarder_active{false};
    /// @brief Count of topic polls that exceeded the configured period.
    std::atomic<std::uint64_t> topic_poll_overruns{0};
    /// @brief Count of shared LiveKit RPC method registration failures.
    std::atomic<std::uint64_t> rpc_register_failures{0};
    /// @brief Count of shared LiveKit outbound RPC failures.
    std::atomic<std::uint64_t> rpc_perform_failures{0};
  };

  /// @brief Poll the topics and create subscribers for the allowed topics
  void pollTopics();

  /// @brief Create components whose LiveKit state belongs to the current room
  /// session.
  /// @return True when all room-bound components are ready.
  bool startRoomComponents();

  /// @brief Destroy components whose LiveKit state belonged to the previous
  /// room session.
  void stopRoomComponents();

  /// @brief Apply participant metadata required for a newly connected room
  /// session.
  /// @return True when the local participant is ready for ROS Portal operations.
  bool prepareRoomSession();

  /// @brief Tear down a terminal room session after the SDK event stream ends.
  void processEndedRoomSession();

  /// @brief Return whether room-facing ROS Portal operations are currently allowed.
  bool roomOperationsEnabled() const;

  /// @brief Handle a remote LiveKit data track being published.
  void onDataTrackPublished(livekit::Room& room, const livekit::DataTrackPublishedEvent& event) override;

  /// @brief Stop republishing a remote LiveKit data track when it is removed.
  void onDataTrackUnpublished(livekit::Room& room, const livekit::DataTrackUnpublishedEvent& event) override;

  // The LiveKit room exposes a single delegate, so ROS Portal owns it and
  // forwards lifecycle events to the connection manager and diagnostics.

  /// @brief Forward participant-connected events to connection diagnostics.
  void onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event) override;

  /// @brief Forward participant-disconnected events to connection diagnostics.
  void onParticipantDisconnected(livekit::Room& room, const livekit::ParticipantDisconnectedEvent& event) override;

  /// @brief Forward connection-state changes to connection diagnostics.
  void onConnectionStateChanged(livekit::Room& room, const livekit::ConnectionStateChangedEvent& event) override;

  /// @brief Forward terminal disconnect events to connection diagnostics.
  void onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) override;

  /// @brief Report when the LiveKit room session reaches end-of-stream.
  void onRoomEos(livekit::Room& room, const livekit::RoomEosEvent& event) override;

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
  /// @return True when removed or the room session has already ended; false on
  /// an active-session cleanup failure.
  bool rpcUnregisterMethod(const std::string& method);

  /// @brief Create TopicForwarder after LiveKit room connection succeeds.
  /// @param topics Configured topics; mapped to TopicForwarder options via
  /// config_mapping (QoS bounds and best-effort patterns come from ROS params).
  /// @return True on success, false when the topic forwarder could not be initialized.
  bool initializeTopicForwarder(const std::vector<ros_portal_config::TopicConfig>& topics);

  /// @brief Create Manager after LiveKit room connection succeeds.
  /// @return True on success, false when the ROS2 CLI manager could not be initialized.
  bool initializeCliManager();

  /// @brief Build the diagnostics registration functions handed to components.
  ///
  /// The returned wrappers close over this node and validate `diagnostics_updater_`
  /// before forwarding, logging FATAL if a component registers or deregisters
  /// a task while the shared updater does not exist.
  diagnostics::DiagnosticsManagerFns makeDiagnosticsFns();

  /// @brief Create the shared updater and register `ros_portal_status`.
  ///
  /// Safe to call again after shutdown; does nothing while an updater exists.
  void initializeDiagnostics();

  /// @brief Populate node lifecycle, configuration, and shared-RPC diagnostics.
  /// @param status Diagnostic status wrapper to populate.
  void populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status);

  /// @brief Create ServiceForwarder independently of LiveKit room availability.
  /// @param services Configured services; outbound routes are derived here.
  /// @return True on success, false when the service forwarder could not be initialized.
  bool initializeServiceForwarder(const std::vector<ros_portal_config::ServiceConfig>& services);

  /// @brief Create LatchedTopicForwarder after LiveKit room connection succeeds.
  ///
  /// Handles topics flagged `latched` (e.g. /tf_static) over a reliable RPC
  /// push-with-ack instead of DataTracks. No-op when no latched topics are
  /// configured.
  /// @param topics Configured topics; latched outbound/inbound sets are derived
  /// here.
  /// @return True on success (including when there is nothing to do), false when
  /// the forwarder could not be initialized.
  bool initializeLatchedTopicForwarder(const std::vector<ros_portal_config::TopicConfig>& topics);

  /// @brief Create, publish, and start all configured capture-backed video sources.
  /// @param video_sources Configured independent LiveKit video sources.
  /// @return True when the manager was constructed. Individual source failures
  /// are isolated and reported through diagnostics.
  bool initializeVideoSources(const std::vector<ros_portal_config::VideoSourceConfig>& video_sources);

  //! @brief The period for polling the topics
  int topic_polling_period_ms_;

  //! @brief The minimum QoS depth
  size_t min_qos_depth_;
  //! @brief The maximum QoS depth
  size_t max_qos_depth_;
  //! @brief Number of threads for the MultiThreadedExecutor (0 = use system
  //! default)
  int ros_threads_;
  //! @brief Tracks whether ROS Portal initialization has completed.
  std::atomic_bool initialized_;
  //! @brief Serializes explicit shutdown with the destructor fallback.
  std::mutex shutdown_mutex_;
  //! @brief Prevents snapshotted publication callbacks from entering during shutdown.
  std::atomic_bool shutting_down_;
  //! @brief Reentrant callback group shared by all subscriptions
  rclcpp::CallbackGroup::SharedPtr reentrant_callback_group_;
  //! @brief The timer for the polling for new topics
  rclcpp::TimerBase::SharedPtr poll_timer_;
  //! @brief Fixed-rate timer for LiveKit room connection attempts.
  rclcpp::TimerBase::SharedPtr connection_timer_;

  //! @brief LiveKit room connection for publishing tracks directly via the SDK.
  std::unique_ptr<livekit::Room> room_;
  //! @brief Shared diagnostics updater for all ROS Portal diagnostic tasks.
  std::unique_ptr<diagnostic_updater::Updater> diagnostics_updater_;
  //! @brief Connection lifecycle, retry state, and session-readiness barrier.
  std::unique_ptr<ConnectionManager> connection_manager_;
  //! @brief Lifetime-safe state gate shared with room-bound callbacks.
  //!
  //! Points at the flag owned by @ref connection_manager_ once that manager
  //! exists.
  std::shared_ptr<std::atomic_bool> room_operations_enabled_{std::make_shared<std::atomic_bool>(false)};
  //! @brief Set by the SDK callback so ROS-thread cleanup precedes reconnect.
  std::atomic_bool room_session_ended_{false};
  //! @brief Whether participant metadata was applied for the current session.
  bool room_session_prepared_{false};
  //! @brief Whether room-session-bound forwarding components are active.
  bool room_components_started_{false};
  //! @brief Serializes room component start, stop, polling, and delegate access.
  std::mutex room_components_mutex_;
  //! @brief Stored topic configuration used to recreate components after reconnect.
  std::vector<ros_portal_config::TopicConfig> topics_;
  //! @brief Stored video source configuration used to recreate sources after reconnect.
  std::vector<ros_portal_config::VideoSourceConfig> video_sources_;
  //! @brief Topic forwarding component for ROS-to-LiveKit and LiveKit-to-ROS.
  std::unique_ptr<TopicForwarder> topic_forwarder_;
  //! @brief Latched-topic (e.g. /tf_static) forwarding over LiveKit RPC.
  std::unique_ptr<LatchedTopicForwarder> latched_topic_forwarder_;
  //! @brief ROS CLI service/RPC manager for remote graph introspection.
  std::unique_ptr<cli::Manager> cli_manager_;
  //! @brief ROS service forwarding component for local proxy services.
  std::unique_ptr<ServiceForwarder> service_forwarder_;
  //! @brief Configured capture-backed LiveKit video sources.
  std::unique_ptr<VideoSourceManager> video_source_manager_;
  //! @brief Always-OK build and dependency version diagnostic task owner.
  std::unique_ptr<diagnostics::BuildInfoDiagnostics> build_info_diagnostics_;
  //! @brief Mutable state owned exclusively for node-level diagnostics.
  DiagnosticState diagnostic_state_;
};

} // namespace ros_portal
