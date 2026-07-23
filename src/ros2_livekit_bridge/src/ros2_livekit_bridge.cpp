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

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"

#include <livekit/data_track_options.h>
#include <livekit/data_track_schema.h>
#include <livekit/data_track_stream.h>
#include <livekit/livekit.h>
#include <livekit/local_data_track.h>
#include <livekit/local_participant.h>
#include <livekit/local_video_track.h>
#include <livekit/remote_data_track.h>
#include <livekit/remote_participant.h>
#include <livekit/room.h>
#include <livekit/rpc_error.h>
#include <livekit/video_source.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <utility>
#include <vector>

#include "ros2_livekit_bridge/cli/manager.hpp"
#include "ros2_livekit_bridge/diagnostics/connection_health.hpp"
#include "ros2_livekit_bridge/latched_topic_forwarder.hpp"
#include "ros2_livekit_bridge/room_connection_manager.hpp"
#include "ros2_livekit_bridge/service_forwarder.hpp"
#include "ros2_livekit_bridge/topic_forwarder.hpp"
#include "ros2_livekit_bridge/utils/config_mapping.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"
#include "ros2_livekit_bridge_config/config/config_parser.hpp"

namespace ros2_livekit_bridge {

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions& options)
    : rclcpp::Node("ros2_livekit_bridge", options),
      topic_polling_period_ms_(0),
      min_qos_depth_(0),
      max_qos_depth_(0),
      ros_threads_(0),
      initialized_(false) {
  this->declare_parameter<std::string>("config_path", "");
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter<int>("min_qos_depth", static_cast<int>(kDefaultMinQosDepth));
  this->declare_parameter<int>("max_qos_depth", static_cast<int>(kDefaultMaxQosDepth));
  this->declare_parameter("best_effort_qos_topics", rclcpp::ParameterValue(kEmptyStringVec));
}

bool Ros2LiveKitBridge::initialize() {
  if (initialized_) {
    RCLCPP_WARN(this->get_logger(), "Bridge is already initialized");
    return true;
  }

  const auto config_path = std::filesystem::path(this->get_parameter("config_path").as_string());
  const auto config = utils::parseBridgeConfig(config_path, this->get_logger());
  if (!config) {
    RCLCPP_FATAL(this->get_logger(), "Failed to parse bridge config");
    return false;
  }

  topic_polling_period_ms_ = config->topic_polling_period_ms;
  ros_threads_ = config->ros_threads;
  connection_diagnostics_ = std::make_unique<diagnostics::ConnectionHealthDiagnostics>(this);

  reentrant_callback_group_ = this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  min_qos_depth_ = static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ = static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());
  topics_ = config->topics;

  RCLCPP_INFO(this->get_logger(), "Polling period: %d ms, %zu configured topics, QoS depth range: [%zu, %zu]",
              topic_polling_period_ms_, config->topics.size(), min_qos_depth_, max_qos_depth_);

  RCLCPP_INFO(this->get_logger(), "Attempting to resolve LiveKit credentials");

  // ----- Resolve LiveKit credentials from environment variables only -----
  std::string url_source, token_source;
  const std::string livekit_url = utils::resolveEnvironmentCredential("LIVEKIT_URL", url_source);
  const std::string livekit_token = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", token_source);

  RCLCPP_INFO(this->get_logger(), "LiveKit URL resolved from %s", url_source.c_str());
  RCLCPP_INFO(this->get_logger(), "LiveKit token resolved from %s", token_source.c_str());

  if (livekit_url.empty() || livekit_token.empty()) {
    RCLCPP_WARN(this->get_logger(),
                "LiveKit credentials not fully provided — bridge will not connect.\n"
                "  livekit_url   : %s\n"
                "  livekit_token : %s\n"
                "Set them via environment variables LIVEKIT_URL / LIVEKIT_TOKEN.",
                livekit_url.empty() ? "(missing)" : url_source.c_str(),
                livekit_token.empty() ? "(missing)" : token_source.c_str());

    return false;
  }

  // The LiveKit SDK lifecycle (livekit::initialize()/shutdown()) is owned by
  // the process entry point (the node main() or the test harness)
  room_ = std::make_unique<livekit::Room>();
  if (!room_) {
    RCLCPP_FATAL(this->get_logger(), "Failed to create LiveKit room");
    return false;
  }
  // Warning: avoid doing ROS operations in delegate callbacks
  room_->setDelegate(this);

  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  room_options.dynacast = true;
  // The bridge owns retry cadence. Disable the Rust SDK's immediate inner join
  // retries so each 1 Hz manager tick represents one connection attempt.
  room_options.join_retries = 0U;

  RoomConnectionManager::Methods connection_methods;
  connection_methods.try_connect = [this, livekit_url, livekit_token, room_options]() {
    return room_ && room_->connect(livekit_url, livekit_token, room_options);
  };
  room_connection_manager_ = std::make_unique<RoomConnectionManager>(std::move(connection_methods),
                                                                     this->get_logger().get_child("connection"));
  connection_diagnostics_->markDisconnected();

  // Room::connect() may emit events for data tracks that were already
  // published. Install the forwarder before the first connection attempt so
  // those events are not dropped while the receiver joins the room.
  if (!initializeTopicForwarder(topics_)) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize topic forwarder");
    return false;
  }

  if (!initializeServiceForwarder(config->services)) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder");
    return false;
  }

  RCLCPP_INFO(this->get_logger(), "Creating timer for polling topics at rate %d ms", topic_polling_period_ms_);

  poll_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(topic_polling_period_ms_), [this]() { pollTopics(); }, reentrant_callback_group_);
  room_connection_timer_ = this->create_wall_timer(
      RoomConnectionManager::kRetryInterval, [this]() { pollRoomConnection(); }, reentrant_callback_group_);
  connection_stats_timer_ =
      this->create_wall_timer(std::chrono::seconds(1), [this]() { pollConnectionStats(); }, reentrant_callback_group_);

  RCLCPP_INFO(this->get_logger(), "Bridge initialized; attempting LiveKit room connection at 1 Hz");
  initialized_ = true;
  return true;
}

Ros2LiveKitBridge::~Ros2LiveKitBridge() {
  poll_timer_.reset();
  room_connection_timer_.reset();
  connection_stats_timer_.reset();
  if (room_connection_manager_) {
    room_connection_manager_->stop();
  }
  service_forwarder_.reset();
  stopRoomComponents();
  if (room_) {
    RCLCPP_INFO(this->get_logger(), "Disconnecting LiveKit room...");
    room_->setDelegate(nullptr);
    room_.reset();
  }
  room_connection_manager_.reset();
  // Note: livekit::shutdown() is intentionally NOT called here — the SDK
  // lifecycle is owned by the process entry point (see initialize()).
}

void Ros2LiveKitBridge::pollRoomConnection() {
  if (!initialized_ || !room_connection_manager_) {
    return;
  }

  if (room_session_ended_.exchange(false)) {
    const std::lock_guard<std::mutex> lock(room_components_mutex_);
    stopRoomComponents();
  }

  if (!room_connection_manager_->isConnected()) {
    const std::lock_guard<std::mutex> lock(room_components_mutex_);
    if (!topic_forwarder_ && !initializeTopicForwarder(topics_)) {
      RCLCPP_ERROR(this->get_logger(), "Failed to initialize topic forwarder before room connection; retrying");
      return;
    }
  }

  const bool was_connected = room_connection_manager_->isConnected();
  room_connection_manager_->poll();
  if (!room_connection_manager_->isConnected()) {
    return;
  }

  if (!was_connected && connection_diagnostics_ && room_) {
    connection_diagnostics_->markConnected(*room_);
  }

  const std::lock_guard<std::mutex> lock(room_components_mutex_);
  if (!startRoomComponents()) {
    RCLCPP_ERROR(this->get_logger(), "Failed to start room-bound bridge components; retrying");
  }
}

bool Ros2LiveKitBridge::startRoomComponents() {
  if (room_components_started_) {
    return true;
  }

  if (!topic_forwarder_ && !initializeTopicForwarder(topics_)) {
    stopRoomComponents();
    return false;
  }
  if (!initializeCliManager()) {
    stopRoomComponents();
    return false;
  }
  if (!initializeLatchedTopicForwarder(topics_)) {
    stopRoomComponents();
    return false;
  }

  room_components_started_ = true;
  return true;
}

void Ros2LiveKitBridge::stopRoomComponents() {
  // Reset before room_ so RPC handlers and workers can release room state.
  cli_manager_.reset();
  latched_topic_forwarder_.reset();
  topic_forwarder_.reset();
  room_components_started_ = false;
}

void Ros2LiveKitBridge::pollTopics() {
  if (!initialized_) {
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                         "Polling topics while bridge is not initialized, skipping...");
    return;
  }
  if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
    return;
  }

  const std::lock_guard<std::mutex> lock(room_components_mutex_);
  if (!room_components_started_) {
    return;
  }

  if (topic_forwarder_) {
    topic_forwarder_->pollTopics();
  }

  if (latched_topic_forwarder_) {
    latched_topic_forwarder_->poll();
  }
}

void Ros2LiveKitBridge::pollConnectionStats() {
  if (room_connection_manager_ && room_connection_manager_->isConnected() && room_ && connection_diagnostics_) {
    connection_diagnostics_->pollStats(*room_);
  }
}

void Ros2LiveKitBridge::onDataTrackPublished(livekit::Room&, const livekit::DataTrackPublishedEvent& event) {
  if (!event.track) {
    RCLCPP_ERROR(this->get_logger(), "Ignoring data track published event with null track pointer");
    return;
  }

  const std::lock_guard<std::mutex> lock(room_components_mutex_);
  if (topic_forwarder_) {
    topic_forwarder_->onDataTrackPublished(event.track);
  }
}

void Ros2LiveKitBridge::onDataTrackUnpublished(livekit::Room&, const livekit::DataTrackUnpublishedEvent& event) {
  const std::lock_guard<std::mutex> lock(room_components_mutex_);
  if (topic_forwarder_) {
    topic_forwarder_->onDataTrackUnpublished(event.sid);
  }
}

void Ros2LiveKitBridge::onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantConnected(room, event);
  }
}

void Ros2LiveKitBridge::onParticipantDisconnected(livekit::Room& room,
                                                  const livekit::ParticipantDisconnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantDisconnected(room, event);
  }
}

void Ros2LiveKitBridge::onConnectionStateChanged(livekit::Room& room,
                                                 const livekit::ConnectionStateChangedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onConnectionStateChanged(room, event);
  }
}

void Ros2LiveKitBridge::onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) {
  if (room_connection_manager_) {
    room_connection_manager_->onDisconnected(static_cast<std::uint32_t>(event.reason));
  }
  if (connection_diagnostics_) {
    connection_diagnostics_->onDisconnected(room, event);
  }
}

void Ros2LiveKitBridge::onRoomEos(livekit::Room&, const livekit::RoomEosEvent&) {
  room_session_ended_.store(true);
  if (room_connection_manager_) {
    room_connection_manager_->onRoomEos();
  }
}

void Ros2LiveKitBridge::onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent& event) {
  if (room_connection_manager_) {
    room_connection_manager_->onReconnecting();
  }
  if (connection_diagnostics_) {
    connection_diagnostics_->onReconnecting(room, event);
  }
}

void Ros2LiveKitBridge::onReconnected(livekit::Room& room, const livekit::ReconnectedEvent& event) {
  if (room_connection_manager_) {
    room_connection_manager_->onReconnected();
  }
  if (connection_diagnostics_) {
    connection_diagnostics_->onReconnected(room, event);
  }
}

void Ros2LiveKitBridge::onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onRoomUpdated(room, event);
  }
}

void Ros2LiveKitBridge::onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantsUpdated(room, event);
  }
}

/// Helpers

bool Ros2LiveKitBridge::initializeTopicForwarder(const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics) {
  try {
    const auto best_effort_qos_topics = this->get_parameter("best_effort_qos_topics").as_string_array();
    auto forwarder_options = utils::topicForwarderOptions(topics, min_qos_depth_, max_qos_depth_,
                                                          best_effort_qos_topics, this->get_logger());

    TopicForwarder::LiveKitMethods forwarder_lk_methods;
    forwarder_lk_methods.publish_data_track = [this](const std::string& topic_name,
                                                     const livekit::DataTrackSchemaId& schema_id)
        -> livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string> {
      const auto participant = room_ ? room_->localParticipant().lock() : nullptr;
      if (!participant) {
        return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure(
            "local participant is unavailable");
      }

      livekit::DataTrackPublishOptions options;
      options.name = topic_name;
      options.schema = schema_id;
      options.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
      const auto publish_result = participant->publishDataTrack(options);
      if (!publish_result) {
        const auto& error = publish_result.error();
        return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure(
            "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + " message=" + error.message);
      }

      auto track = publish_result.value();
      if (!track) {
        return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::failure(
            "publishDataTrack returned null track");
      }

      auto writer = std::make_shared<TopicForwarder::DataTrackWriter>();
      writer->try_push = [this, track = std::move(track)](std::vector<std::uint8_t> payload) {
        if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
          return livekit::Result<void, std::string>::success();
        }
        const auto push_result = track->tryPush(std::move(payload));
        if (!push_result) {
          const auto& error = push_result.error();
          return livekit::Result<void, std::string>::failure(
              "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + " message=" + error.message);
        }
        return livekit::Result<void, std::string>::success();
      };
      return livekit::Result<std::shared_ptr<TopicForwarder::DataTrackWriter>, std::string>::success(std::move(writer));
    };

    forwarder_lk_methods.schema.define_schema = [this](const livekit::DataTrackSchemaId& schema_id,
                                                       const std::string& schema_text) {
      const auto participant = room_ ? room_->localParticipant().lock() : nullptr;
      if (!participant) {
        return false;
      }
      return participant->defineSchema(schema_id, schema_text);
    };

    forwarder_lk_methods.schema.get_schema =
        [this](const livekit::DataTrackSchemaId& schema_id,
               const std::string& participant_identity) -> std::optional<std::string> {
      const auto participant = room_ ? room_->localParticipant().lock() : nullptr;
      if (!participant) {
        return std::nullopt;
      }
      return participant->getSchema(schema_id, participant_identity);
    };

    forwarder_lk_methods.publish_video_track =
        [this](const std::string& topic_name, int width,
               int height) -> livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string> {
      const auto participant = room_ ? room_->localParticipant().lock() : nullptr;
      if (!participant) {
        return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure(
            "local participant is unavailable");
      }

      try {
        auto source = std::make_shared<livekit::VideoSource>(width, height);
        auto track = participant->publishVideoTrack(topic_name, source, livekit::TrackSource::SOURCE_CAMERA);
        if (!track) {
          return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure(
              "publishVideoTrack returned null track");
        }
        auto sink = std::make_shared<TopicForwarder::VideoTrackSink>();
        sink->width = width;
        sink->height = height;
        sink->capture_frame = [this, source = std::move(source), track = std::move(track)](
                                  const livekit::VideoFrame& frame, std::int64_t timestamp_us) {
          if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
            return;
          }
          (void)track;
          source->captureFrame(frame, timestamp_us);
        };
        return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::success(std::move(sink));
      } catch (const std::exception& error) {
        return livekit::Result<std::shared_ptr<TopicForwarder::VideoTrackSink>, std::string>::failure(error.what());
      }
    };

    topic_forwarder_ = std::make_unique<TopicForwarder>(std::move(forwarder_options),
                                                        this->weak_from_this(), // weak_from_this() MUST be called after
                                                                                // constructor
                                                        std::move(forwarder_lk_methods));
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize topic forwarder, unknown exception");
    return false;
  }
  return topic_forwarder_ != nullptr;
}

bool Ros2LiveKitBridge::initializeCliManager() {
  try {
    cli::Manager::LiveKitMethods cli_lk_methods{
        [this](const std::string& id) { return hasParticipant(id); },
        [this](const std::string& id, const std::string& method, const std::string& payload, std::uint8_t timeout_sec) {
          return rpcPerform(id, method, payload, timeout_sec);
        },
        [this](const std::string& method, RpcHandler handler) { return rpcRegisterMethod(method, std::move(handler)); },
        [this](const std::string& method) { return rpcUnregisterMethod(method); },
    };
    const auto topic_publish_allowed = [this](const std::string& topic_name) {
      return topic_forwarder_ && topic_forwarder_->isIncomingTopicAllowed(topic_name);
    };
    cli_manager_ = std::make_unique<cli::Manager>(*this, reentrant_callback_group_, std::move(cli_lk_methods),
                                                  std::move(topic_publish_allowed));
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize ROS2 CLI manager, unknown exception");
    return false;
  }

  return cli_manager_ != nullptr;
}

bool Ros2LiveKitBridge::initializeServiceForwarder(
    const std::vector<ros2_livekit_bridge_config::ServiceConfig>& services) {
  try {
    const ServiceForwarder::LiveKitMethods livekit_methods{
        [this](const std::string& id) { return hasParticipant(id); },
        [this](const std::string& id, const std::string& method, const std::string& payload, std::uint8_t timeout_sec) {
          return rpcPerform(id, method, payload, timeout_sec);
        },
    };
    service_forwarder_ = std::make_unique<ServiceForwarder>(utils::outgoingServiceRoutes(services), *this,
                                                            reentrant_callback_group_, livekit_methods);
  } catch (const std::exception& error) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder: %s", error.what());
    return false;
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder, unknown exception");
    return false;
  }

  return service_forwarder_ != nullptr;
}

bool Ros2LiveKitBridge::initializeLatchedTopicForwarder(
    const std::vector<ros2_livekit_bridge_config::TopicConfig>& topics) {
  auto options = utils::latchedTopicForwarderOptions(topics);
  if (options.outbound_topics.empty() && options.inbound_topics.empty()) {
    RCLCPP_INFO(this->get_logger(), "No latched topics configured; skipping latched topic forwarder");
    return true;
  }

  try {
    LatchedTopicForwarder::LiveKitMethods methods;
    methods.register_rpc_method = [this](const std::string& method, RpcHandler handler) {
      return rpcRegisterMethod(method, std::move(handler));
    };
    methods.unregister_rpc_method = [this](const std::string& method) { return rpcUnregisterMethod(method); };
    methods.perform_rpc = [this](const std::string& id, const std::string& method, const std::string& payload,
                                 std::uint8_t timeout_sec) { return rpcPerform(id, method, payload, timeout_sec); };
    methods.list_remote_identities = [this]() {
      std::vector<std::string> identities;
      if (!room_connection_manager_ || !room_connection_manager_->isConnected() || !room_) {
        return identities;
      }
      for (const auto& weak_participant : room_->remoteParticipants()) {
        if (const auto participant = weak_participant.lock()) {
          identities.push_back(participant->identity());
        }
      }
      return identities;
    };

    latched_topic_forwarder_ = std::make_unique<LatchedTopicForwarder>(std::move(options),
                                                                       this->weak_from_this(), // after constructor
                                                                       std::move(methods));
    latched_topic_forwarder_->start();
  } catch (const std::exception& error) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize latched topic forwarder: %s", error.what());
    return false;
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize latched topic forwarder, unknown exception");
    return false;
  }

  return latched_topic_forwarder_ != nullptr;
}

bool Ros2LiveKitBridge::hasParticipant(const std::string& participant_id) const {
  if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
    return false;
  }
  if (!room_) {
    RCLCPP_ERROR(this->get_logger(), "Room is not available, cannot check for participant '%s'",
                 participant_id.c_str());
    return false;
  }
  return static_cast<bool>(room_->remoteParticipant(participant_id).lock());
}

std::optional<std::string> Ros2LiveKitBridge::rpcPerform(const std::string& participant_id, const std::string& method,
                                                         const std::string& payload, std::uint8_t timeout_sec) {
  if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
    return std::nullopt;
  }
  const auto local_participant = room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    RCLCPP_ERROR(this->get_logger(),
                 "LiveKit RPC '%s' to participant '%s' failed: local participant "
                 "is unavailable",
                 method.c_str(), participant_id.c_str());
    return std::nullopt;
  }

  try {
    return local_participant->performRpc(participant_id, method, payload, static_cast<double>(timeout_sec));
  } catch (const livekit::RpcError& error) {
    RCLCPP_ERROR(this->get_logger(), "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s", method.c_str(),
                 participant_id.c_str(), error.code(), error.message().c_str());
    return std::nullopt;
  }
}

bool Ros2LiveKitBridge::rpcRegisterMethod(const std::string& method, RpcHandler handler) {
  if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
    return false;
  }
  const auto local_participant = room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    RCLCPP_WARN(this->get_logger(),
                "Cannot register RPC method '%s': LiveKit local participant is "
                "unavailable",
                method.c_str());
    return false;
  }

  try {
    local_participant->registerRpcMethod(
        method, [handler = std::move(handler)](const livekit::RpcInvocationData& data) -> std::optional<std::string> {
          return handler(data.payload);
        });
  } catch (const livekit::RpcError& error) {
    RCLCPP_ERROR(this->get_logger(), "LiveKit RPC method '%s' registration failed: code=%u message=%s", method.c_str(),
                 error.code(), error.message().c_str());
    return false;
  }
  return true;
}

bool Ros2LiveKitBridge::rpcUnregisterMethod(const std::string& method) {
  if (!room_connection_manager_ || !room_connection_manager_->isConnected()) {
    return false;
  }
  const auto local_participant = room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    RCLCPP_WARN(this->get_logger(),
                "Cannot unregister RPC method '%s': LiveKit local participant "
                "is unavailable",
                method.c_str());
    return false;
  }

  try {
    local_participant->unregisterRpcMethod(method);
  } catch (const livekit::RpcError& error) {
    RCLCPP_ERROR(this->get_logger(), "LiveKit RPC method '%s' unregistration failed: code=%u message=%s",
                 method.c_str(), error.code(), error.message().c_str());
    return false;
  }
  return true;
}

} // namespace ros2_livekit_bridge
