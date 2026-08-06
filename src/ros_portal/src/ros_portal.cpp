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

#include "ros_portal/ros_portal.hpp"

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
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "ros_portal/cli/manager.hpp"
#include "ros_portal/diagnostics/build_info.hpp"
#include "ros_portal/diagnostics/connection_health.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/latched_topic_forwarder.hpp"
#include "ros_portal/service_forwarder.hpp"
#include "ros_portal/topic_forwarder.hpp"
#include "ros_portal/utils/config_mapping.hpp"
#include "ros_portal/utils/ros_utils.hpp"
#include "ros_portal_config/config/config_parser.hpp"

namespace ros_portal {

namespace {

/// Diagnostic task name for node lifecycle and shared infrastructure.
constexpr char kRosPortalStatusDiagnosticTaskName[] = "ros_portal_status";

} // namespace

RosPortal::RosPortal(const rclcpp::NodeOptions& options)
    : rclcpp::Node("ros_portal", options),
      topic_polling_period_ms_(0),
      min_qos_depth_(0),
      max_qos_depth_(0),
      ros_threads_(0),
      initialized_(false),
      shutting_down_(false) {
  this->declare_parameter<std::string>("config_path", "");
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter<int>("min_qos_depth", static_cast<int>(kDefaultMinQosDepth));
  this->declare_parameter<int>("max_qos_depth", static_cast<int>(kDefaultMaxQosDepth));
  this->declare_parameter("best_effort_qos_topics", rclcpp::ParameterValue(kEmptyStringVec));

  {
    const std::lock_guard<std::mutex> lock(diagnostic_state_.metadata_mutex);
    const auto config_path = this->get_parameter("config_path").as_string();
    diagnostic_state_.config_path = config_path.empty() ? "unset" : config_path;
  }
  initializeDiagnostics();
}

bool RosPortal::initialize() {
  if (initialized_.load(std::memory_order_relaxed)) {
    RCLCPP_WARN(this->get_logger(), "ROS Portal is already initialized");
    return true;
  }

  initializeDiagnostics();
  shutting_down_.store(false, std::memory_order_relaxed);

  const auto config_path = std::filesystem::path(this->get_parameter("config_path").as_string());
  {
    const std::lock_guard<std::mutex> lock(diagnostic_state_.metadata_mutex);
    diagnostic_state_.config_path = config_path.empty() ? "unset" : config_path.string();
  }
  const auto config = utils::parseRosPortalConfig(config_path, this->get_logger());
  if (!config) {
    RCLCPP_FATAL(this->get_logger(), "Failed to parse ROS Portal config");
    return false;
  }

  topic_polling_period_ms_ = config->topic_polling_period_ms;
  ros_threads_ = config->ros_threads;
  diagnostic_state_.topic_polling_period_ms.store(topic_polling_period_ms_, std::memory_order_relaxed);
  topic_forwarder_.reset();
  diagnostic_state_.topic_forwarder_active.store(false, std::memory_order_relaxed);
  connection_diagnostics_.reset();
  diagnostic_state_.connection_health_active.store(false, std::memory_order_relaxed);
  build_info_diagnostics_.reset();
  connection_diagnostics_ = std::make_unique<diagnostics::ConnectionHealthDiagnostics>(makeDiagnosticsFns());
  build_info_diagnostics_ = std::make_unique<diagnostics::BuildInfoDiagnostics>(makeDiagnosticsFns());
  diagnostic_state_.connection_health_active.store(connection_diagnostics_ != nullptr, std::memory_order_relaxed);

  reentrant_callback_group_ = this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  min_qos_depth_ = static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ = static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());

  RCLCPP_INFO(this->get_logger(),
              "Polling period: %d ms, %zu configured topics, QoS depth range: [%zu, %zu], ros_threads: %d",
              topic_polling_period_ms_, config->topics.size(), min_qos_depth_, max_qos_depth_, ros_threads_);

  RCLCPP_INFO(this->get_logger(), "Attempting to resolve LiveKit credentials");

  // ----- Resolve LiveKit credentials from environment variables only -----
  std::string url_source, token_source;
  const std::string livekit_url = utils::resolveEnvironmentCredential("LIVEKIT_URL", url_source);
  const std::string livekit_token = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", token_source);

  RCLCPP_INFO(this->get_logger(), "LiveKit URL resolved from %s", url_source.c_str());
  RCLCPP_INFO(this->get_logger(), "LiveKit token resolved from %s", token_source.c_str());

  RCLCPP_INFO(this->get_logger(), "Creating default room options");
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  room_options.dynacast = true;

  if (livekit_url.empty() || livekit_token.empty()) {
    RCLCPP_WARN(this->get_logger(),
                "LiveKit credentials not fully provided — ROS Portal will not connect.\n"
                "  livekit_url   : %s\n"
                "  livekit_token : %s\n"
                "Set them via environment variables LIVEKIT_URL / LIVEKIT_TOKEN.",
                livekit_url.empty() ? "(missing)" : url_source.c_str(),
                livekit_token.empty() ? "(missing)" : token_source.c_str());

    return false;
  }

  RCLCPP_INFO(this->get_logger(), "Valid credentials found. Connecting to %s ...", livekit_url.c_str());
  // The LiveKit SDK lifecycle (livekit::initialize()/shutdown()) is owned by
  // the process entry point (the node main() or the test harness)
  room_ = std::make_unique<livekit::Room>();
  if (!room_) {
    RCLCPP_FATAL(this->get_logger(), "Failed to create LiveKit room");
    return false;
  }
  // Warning: avoid doing ROS operations in delegate callbacks
  {
    const std::lock_guard<std::mutex> callback_lock(data_track_callback_mutex_);
    shutting_down_.store(false, std::memory_order_relaxed);
  }
  room_->setDelegate(this);

  // Room::connect() may emit events for data tracks that were already
  // published. Install the forwarder first so those events are not dropped
  // while the receiver joins the room.
  if (!initializeTopicForwarder(config->topics)) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize topic forwarder");
    return false;
  }

  if (!room_->connect(livekit_url, livekit_token, room_options)) {
    connection_diagnostics_->markDisconnected();
    room_.reset();
    RCLCPP_FATAL(this->get_logger(), "Failed to connect to LiveKit room.");
    return false;
  }

  connection_diagnostics_->markConnected(*room_);

  if (auto lp = room_->localParticipant().lock()) {
    auto attributes = lp->attributes();
    attributes[kRobotParticipantAttribute] = "true";
    lp->setAttributes(attributes);

    {
      const std::lock_guard<std::mutex> lock(diagnostic_state_.metadata_mutex);
      diagnostic_state_.local_identity = lp->identity();
    }
    RCLCPP_INFO(this->get_logger(), "Connected to LiveKit room '%s' with identity '%s'", room_->roomInfo().name.c_str(),
                lp->identity().c_str());
  } else {
    RCLCPP_FATAL(this->get_logger(), "Failed to get local participant");
    return false;
  }

  if (!initializeCliManager()) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize ROS2 CLI manager");
    return false;
  }

  if (!initializeServiceForwarder(config->services)) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder");
    return false;
  }

  if (!initializeLatchedTopicForwarder(config->topics)) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize latched topic forwarder");
    return false;
  }

  poll_timer_ = this->create_wall_timer(std::chrono::milliseconds(topic_polling_period_ms_),
                                        std::bind(&RosPortal::pollTopics, this), reentrant_callback_group_);
  RCLCPP_INFO(this->get_logger(), "Topic poll timer active (period %d ms)", topic_polling_period_ms_);
  connection_stats_timer_ = this->create_wall_timer(
      std::chrono::seconds(1), std::bind(&RosPortal::pollConnectionStats, this), reentrant_callback_group_);

  // ROS Portal is considered initialized only once it is connected to a room
  // (room_ is left null when credentials are absent or connection failed).
  initialized_.store(room_ != nullptr, std::memory_order_relaxed);
  return initialized_.load(std::memory_order_relaxed);
}

RosPortal::~RosPortal() { shutdown(); }

void RosPortal::shutdown() {
  const std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
  shutting_down_.store(true, std::memory_order_relaxed);
  if (diagnostics_updater_) {
    // Publish the lifecycle transition before the updater and component tasks
    // are dismantled; otherwise the brief shutting-down state is rarely visible.
    diagnostics_updater_->force_update();
  }

  // Stop ROS callbacks before dismantling the objects they access.
  if (poll_timer_) {
    poll_timer_->cancel();
    poll_timer_.reset();
  }
  if (connection_stats_timer_) {
    connection_stats_timer_->cancel();
    connection_stats_timer_.reset();
  }

  // The SDK stores a raw delegate pointer. Detach it before disconnecting so
  // teardown events cannot call back into a partially destroyed ROS Portal node.
  if (room_) {
    room_->setDelegate(nullptr);
  }

  // Let an in-flight data-track publication finish while the room is usable.
  // A callback captured just before setDelegate(nullptr) observes the shutdown
  // flag and returns without touching the topic forwarder.
  {
    const std::lock_guard<std::mutex> callback_lock(data_track_callback_mutex_);
    shutting_down_.store(true, std::memory_order_relaxed);
  }

  // These components own RPC handlers rather than RoomDelegate callbacks.
  // Release them while the local participant is still available so they can
  // unregister cleanly.
  service_forwarder_.reset();
  diagnostic_state_.service_forwarder_active.store(false, std::memory_order_relaxed);
  cli_manager_.reset();
  diagnostic_state_.cli_manager_active.store(false, std::memory_order_relaxed);
  latched_topic_forwarder_.reset();
  diagnostic_state_.latched_topic_forwarder_active.store(false, std::memory_order_relaxed);

  // Disconnect before destroying delegate targets. Room::disconnect() removes
  // its listener only after in-flight callbacks complete.
  if (room_) {
    RCLCPP_INFO(this->get_logger(), "Disconnecting LiveKit room...");
    try {
      (void)(room_->disconnect());
    } catch (const std::exception& error) {
      RCLCPP_ERROR(this->get_logger(), "Failed to disconnect LiveKit room during shutdown: %s", error.what());
    } catch (...) {
      RCLCPP_ERROR(this->get_logger(), "Failed to disconnect LiveKit room during shutdown");
    }
  }

  topic_forwarder_.reset();
  diagnostic_state_.topic_forwarder_active.store(false, std::memory_order_relaxed);
  connection_diagnostics_.reset();
  diagnostic_state_.connection_health_active.store(false, std::memory_order_relaxed);
  build_info_diagnostics_.reset();

  // Reset diagnostics_updater_ after all its task owners are gone.
  initialized_.store(false, std::memory_order_relaxed);
  diagnostics_updater_.reset();
  room_.reset();

  // Note: livekit::shutdown() is intentionally NOT called here — the SDK
  // lifecycle is owned by the process entry point (see initialize()).
}

diagnostics::DiagnosticsManagerFns RosPortal::makeDiagnosticsFns() {
  // Components are owned by this node and destroyed before it, so capturing
  // `this` is safe for the lifetime of every wrapper handed out here.
  diagnostics::DiagnosticsManagerFns fns;
  fns.add = [this](const std::string& name, diagnostics::DiagnosticsManagerFns::TaskCallback callback) {
    if (diagnostics_updater_ == nullptr) {
      RCLCPP_FATAL(this->get_logger(), "Cannot register diagnostic task '%s': diagnostics updater does not exist",
                   name.c_str());
      return;
    }
    diagnostics_updater_->removeByName(name);
    diagnostics_updater_->add(name, std::move(callback));
  };
  fns.remove = [this](const std::string& name) {
    if (diagnostics_updater_ == nullptr) {
      RCLCPP_FATAL(this->get_logger(), "Cannot deregister diagnostic task '%s': diagnostics updater does not exist",
                   name.c_str());
      return;
    }
    diagnostics_updater_->removeByName(name);
  };
  return fns;
}

void RosPortal::initializeDiagnostics() {
  if (diagnostics_updater_ != nullptr) {
    return;
  }

  diagnostics_updater_ = std::make_unique<diagnostic_updater::Updater>(this);
  diagnostics_updater_->setHardwareID("ros_portal");
  diagnostics_updater_->add(kRosPortalStatusDiagnosticTaskName,
                            [this](diagnostic_updater::DiagnosticStatusWrapper& status) { populateStatus(status); });
}

void RosPortal::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) {
  std::string config_path;
  std::string local_identity;
  {
    const std::lock_guard<std::mutex> lock(diagnostic_state_.metadata_mutex);
    config_path = diagnostic_state_.config_path;
    local_identity = diagnostic_state_.local_identity;
  }

  std::string components_inactive;
  const auto append_component = [&components_inactive](const char* component) {
    if (!components_inactive.empty()) {
      components_inactive += ",";
    }
    components_inactive += component;
  };
  if (!diagnostic_state_.connection_health_active.load(std::memory_order_relaxed)) {
    append_component("connection_health");
  }
  if (!diagnostic_state_.topic_forwarder_active.load(std::memory_order_relaxed)) {
    append_component("topic_forwarder");
  }
  if (!diagnostic_state_.latched_topic_forwarder_active.load(std::memory_order_relaxed)) {
    append_component("latched_topic_forwarder");
  }
  if (!diagnostic_state_.service_forwarder_active.load(std::memory_order_relaxed)) {
    append_component("service_forwarder");
  }
  if (!diagnostic_state_.cli_manager_active.load(std::memory_order_relaxed)) {
    append_component("cli_manager");
  }

  const bool has_inactive_components = !components_inactive.empty();

  const bool initialized = initialized_.load(std::memory_order_relaxed);
  const bool poll_timer_active = poll_timer_ != nullptr;
  const auto topic_poll_overruns = diagnostic_state_.topic_poll_overruns.load(std::memory_order_relaxed);

  status.add("initialized", initialized ? "true" : "false");
  status.add("components_inactive", has_inactive_components ? components_inactive : "none");
  status.add("config_path", config_path);
  status.add("topic_polling_period_ms", diagnostic_state_.topic_polling_period_ms.load(std::memory_order_relaxed));
  status.add("local_identity", local_identity);
  status.add("rpc_register_failures", diagnostic_state_.rpc_register_failures.load(std::memory_order_relaxed));
  status.add("rpc_perform_failures", diagnostic_state_.rpc_perform_failures.load(std::memory_order_relaxed));
  status.add("topic_poll_overruns", topic_poll_overruns);

  if (!initialized) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "ROS Portal is not initialized");
  } else if (!poll_timer_active) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                   "ROS Portal is initialized without an active topic poll timer");
  } else if (has_inactive_components) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "ROS Portal has inactive components");
  } else if (topic_poll_overruns > 0) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "ROS Portal topic polling has overrun");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "ROS Portal is initialized");
  }
}

void RosPortal::pollTopics() {
  if (!initialized_.load(std::memory_order_relaxed)) {
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                         "Polling topics while ROS Portal is not initialized, skipping...");
    return;
  }

  const auto poll_started = std::chrono::steady_clock::now();
  if (topic_forwarder_) {
    topic_forwarder_->pollTopics();
  }

  if (latched_topic_forwarder_) {
    latched_topic_forwarder_->poll();
  }

  if (std::chrono::steady_clock::now() - poll_started >
      std::chrono::milliseconds(diagnostic_state_.topic_polling_period_ms.load(std::memory_order_relaxed))) {
    diagnostic_state_.topic_poll_overruns.fetch_add(1, std::memory_order_relaxed);
  }
}

void RosPortal::pollConnectionStats() {
  if (room_ && connection_diagnostics_) {
    connection_diagnostics_->pollStats(*room_);
  }
}

void RosPortal::onDataTrackPublished(livekit::Room&, const livekit::DataTrackPublishedEvent& event) {
  const std::lock_guard<std::mutex> callback_lock(data_track_callback_mutex_);
  if (shutting_down_.load(std::memory_order_relaxed)) {
    return;
  }
  if (!event.track) {
    RCLCPP_ERROR(this->get_logger(), "Ignoring data track published event with null track pointer");
    return;
  }

  if (topic_forwarder_) {
    topic_forwarder_->onDataTrackPublished(event.track);
  }
}

void RosPortal::onDataTrackUnpublished(livekit::Room&, const livekit::DataTrackUnpublishedEvent& event) {
  if (topic_forwarder_) {
    topic_forwarder_->onDataTrackUnpublished(event.sid);
  }
}

void RosPortal::onParticipantConnected(livekit::Room& room, const livekit::ParticipantConnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantConnected(room, event);
  }
}

void RosPortal::onParticipantDisconnected(livekit::Room& room, const livekit::ParticipantDisconnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantDisconnected(room, event);
  }
}

void RosPortal::onConnectionStateChanged(livekit::Room& room, const livekit::ConnectionStateChangedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onConnectionStateChanged(room, event);
  }
}

void RosPortal::onDisconnected(livekit::Room& room, const livekit::DisconnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onDisconnected(room, event);
  }
}

void RosPortal::onReconnecting(livekit::Room& room, const livekit::ReconnectingEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onReconnecting(room, event);
  }
}

void RosPortal::onReconnected(livekit::Room& room, const livekit::ReconnectedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onReconnected(room, event);
  }
}

void RosPortal::onRoomUpdated(livekit::Room& room, const livekit::RoomUpdatedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onRoomUpdated(room, event);
  }
}

void RosPortal::onParticipantsUpdated(livekit::Room& room, const livekit::ParticipantsUpdatedEvent& event) {
  if (connection_diagnostics_) {
    connection_diagnostics_->onParticipantsUpdated(room, event);
  }
}

/// Helpers

bool RosPortal::initializeTopicForwarder(const std::vector<ros_portal_config::TopicConfig>& topics) {
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
      // The schema encoding dictates the wire format: a JsonSchema schema
      // describes self-describing JSON frames; every other supported schema
      // (Ros2Msg, Ros2Idl) describes raw ROS CDR frames.
      options.frame_encoding = schema_id.encoding == livekit::DataTrackSchemaEncoding::JsonSchema
                                   ? livekit::DataTrackFrameEncoding::Json
                                   : livekit::DataTrackFrameEncoding::Cdr;
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
      writer->try_push = [track = std::move(track)](std::vector<std::uint8_t> payload) {
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
        sink->capture_frame = [source = std::move(source), track = std::move(track)](const livekit::VideoFrame& frame,
                                                                                     std::int64_t timestamp_us) {
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
                                                        std::move(forwarder_lk_methods), makeDiagnosticsFns());
    diagnostic_state_.topic_forwarder_active.store(topic_forwarder_ != nullptr, std::memory_order_relaxed);
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize topic forwarder, unknown exception");
    return false;
  }
  return topic_forwarder_ != nullptr;
}

bool RosPortal::initializeCliManager() {
  try {
    const cli::Manager::LiveKitMethods cli_lk_methods{
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
                                                  std::move(topic_publish_allowed), makeDiagnosticsFns());
    diagnostic_state_.cli_manager_active.store(cli_manager_ != nullptr, std::memory_order_relaxed);
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize ROS2 CLI manager, unknown exception");
    return false;
  }

  return cli_manager_ != nullptr;
}

bool RosPortal::initializeServiceForwarder(const std::vector<ros_portal_config::ServiceConfig>& services) {
  try {
    const ServiceForwarder::LiveKitMethods livekit_methods{
        [this](const std::string& id) { return hasParticipant(id); },
        [this](const std::string& id, const std::string& method, const std::string& payload, std::uint8_t timeout_sec) {
          return rpcPerform(id, method, payload, timeout_sec);
        },
    };
    service_forwarder_ = std::make_unique<ServiceForwarder>(utils::outgoingServiceRoutes(services), *this,
                                                            reentrant_callback_group_, livekit_methods);
    diagnostic_state_.service_forwarder_active.store(service_forwarder_ != nullptr, std::memory_order_relaxed);
  } catch (const std::exception& error) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder: %s", error.what());
    return false;
  } catch (...) {
    RCLCPP_FATAL(this->get_logger(), "Failed to initialize service forwarder, unknown exception");
    return false;
  }

  return service_forwarder_ != nullptr;
}

bool RosPortal::initializeLatchedTopicForwarder(const std::vector<ros_portal_config::TopicConfig>& topics) {
  auto options = utils::latchedTopicForwarderOptions(topics);
  if (options.outbound_topics.empty() && options.inbound_topics.empty()) {
    diagnostic_state_.latched_topic_forwarder_active.store(false, std::memory_order_relaxed);
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
      if (!room_) {
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
    diagnostic_state_.latched_topic_forwarder_active.store(latched_topic_forwarder_ != nullptr,
                                                           std::memory_order_relaxed);
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

bool RosPortal::hasParticipant(const std::string& participant_id) const {
  if (!room_) {
    RCLCPP_ERROR(this->get_logger(), "Room is not available, cannot check for participant '%s'",
                 participant_id.c_str());
    return false;
  }
  return static_cast<bool>(room_->remoteParticipant(participant_id).lock());
}

std::optional<std::string> RosPortal::rpcPerform(const std::string& participant_id, const std::string& method,
                                                 const std::string& payload, std::uint8_t timeout_sec) {
  const auto local_participant = room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    diagnostic_state_.rpc_perform_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(this->get_logger(),
                 "LiveKit RPC '%s' to participant '%s' failed: local participant "
                 "is unavailable",
                 method.c_str(), participant_id.c_str());
    return std::nullopt;
  }

  try {
    return local_participant->performRpc(participant_id, method, payload, static_cast<double>(timeout_sec));
  } catch (const livekit::RpcError& error) {
    diagnostic_state_.rpc_perform_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(this->get_logger(), "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s", method.c_str(),
                 participant_id.c_str(), error.code(), error.message().c_str());
    return std::nullopt;
  }
}

bool RosPortal::rpcRegisterMethod(const std::string& method, RpcHandler handler) {
  const auto local_participant = room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    diagnostic_state_.rpc_register_failures.fetch_add(1, std::memory_order_relaxed);
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
    diagnostic_state_.rpc_register_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(this->get_logger(), "LiveKit RPC method '%s' registration failed: code=%u message=%s", method.c_str(),
                 error.code(), error.message().c_str());
    return false;
  }
  return true;
}

bool RosPortal::rpcUnregisterMethod(const std::string& method) {
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

} // namespace ros_portal
