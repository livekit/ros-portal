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
#include "ros2_livekit_bridge/ros2_cli_manager.hpp"
#include "ros2_livekit_bridge/utils/image_conversion.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

#include <livekit/data_track_frame.h>
#include <livekit/data_track_stream.h>
#include <livekit/livekit.h>
#include <livekit/local_participant.h>
#include <livekit/room.h>
#include <livekit/rpc_error.h>

namespace ros2_livekit_bridge
{

namespace
{

constexpr size_t DEFAULT_MIN_QOS_DEPTH = 1;
constexpr size_t DEFAULT_MAX_QOS_DEPTH = 25;
constexpr const char *kImageMsgType = "sensor_msgs/msg/Image";

} // namespace

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions & options)
: rclcpp::Node("ros2_livekit_bridge", options), topic_polling_period_ms_(0),
  min_qos_depth_(0), max_qos_depth_(0), ros_threads_(0),
  initialized_(false)
{
  this->declare_parameter<std::string>("config_path", "");
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter<int>("min_qos_depth",
                               static_cast<int>(DEFAULT_MIN_QOS_DEPTH));
  this->declare_parameter<int>("max_qos_depth",
                               static_cast<int>(DEFAULT_MAX_QOS_DEPTH));
  this->declare_parameter("best_effort_qos_topics",
                          rclcpp::ParameterValue(kEmptyStringVec));
}

bool Ros2LiveKitBridge::initialize()
{
  if (initialized_) {
    RCLCPP_WARN(this->get_logger(), "Bridge is already initialized");
    return true;
  }

  const auto config_path =
    std::filesystem::path(this->get_parameter("config_path").as_string());
  const auto config =
    utils::parseBridgeConfig(config_path, this->get_logger());
  if (!config) {
    return false;
  }

  room_name_ = config->room_name;
  topic_polling_period_ms_ = config->topic_polling_period_ms;
  ros_threads_ = config->ros_threads;

  reentrant_callback_group_ =
    this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  min_qos_depth_ =
    static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ =
    static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());
  outgoing_topic_patterns_ = utils::outgoingTopicPatterns(*config);
  std::vector<utils::PatternCompileError> pattern_errors;
  outgoing_topic_compiled_patterns_ =
    utils::compileRegexPatterns(
      outgoing_topic_patterns_, &pattern_errors);
  utils::logPatternCompileErrors(pattern_errors, this->get_logger());

  auto best_effort_topics =
    this->get_parameter("best_effort_qos_topics").as_string_array();
  pattern_errors.clear();
  best_effort_qos_topic_patterns_ =
    utils::compileRegexPatterns(best_effort_topics, &pattern_errors);
  utils::logPatternCompileErrors(pattern_errors, this->get_logger());

  incoming_topic_patterns_ = utils::incomingTopicPatterns(*config);
  pattern_errors.clear();
  incoming_topic_compiled_patterns_ =
    utils::compileRegexPatterns(
      incoming_topic_patterns_, &pattern_errors);
  utils::logPatternCompileErrors(pattern_errors, this->get_logger());

  RCLCPP_INFO(this->get_logger(),
              "Room: '%s', polling period: %d ms, watching %zu ROS topic "
              "patterns, %zu LiveKit-to-ROS topic patterns, QoS depth range: "
              "[%zu, %zu]",
              room_name_.c_str(), topic_polling_period_ms_,
              outgoing_topic_compiled_patterns_.size(),
              incoming_topic_compiled_patterns_.size(),
              min_qos_depth_, max_qos_depth_);

  RCLCPP_INFO(this->get_logger(), "Attempting to resolve LiveKit credentials");

  // ----- Resolve LiveKit credentials from environment variables only -----
  std::string url_source, token_source;
  const std::string livekit_url =
    utils::resolveEnvironmentCredential("LIVEKIT_URL", url_source);
  const std::string livekit_token =
    utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", token_source);

  RCLCPP_INFO(this->get_logger(), "LiveKit URL resolved from %s",
              url_source.c_str());
  RCLCPP_INFO(this->get_logger(), "LiveKit token resolved from %s",
              token_source.c_str());

  RCLCPP_INFO(this->get_logger(), "Creating default room options");
  livekit::RoomOptions room_options;
  room_options.auto_subscribe = true;
  room_options.dynacast = true;

  if (livekit_url.empty() || livekit_token.empty()) {
    RCLCPP_WARN(
        this->get_logger(),
        "LiveKit credentials not fully provided — bridge will not connect.\n"
        "  livekit_url   : %s\n"
        "  livekit_token : %s\n"
        "Set them via environment variables LIVEKIT_URL / LIVEKIT_TOKEN.",
        livekit_url.empty() ? "(missing)" : url_source.c_str(),
        livekit_token.empty() ? "(missing)" : token_source.c_str());
  } else {
    RCLCPP_INFO(this->get_logger(), "livekit_url   resolved from %s",
                url_source.c_str());
    RCLCPP_INFO(this->get_logger(), "livekit_token resolved from %s",
                token_source.c_str());
    RCLCPP_INFO(this->get_logger(), "Connecting to %s ...",
                livekit_url.c_str());
    // The LiveKit SDK lifecycle (livekit::initialize()/shutdown()) is owned by
    // the process entry point (the node main() or the test harness), not by
    // this node: livekit::initialize() must be the first LiveKit API called in
    // the process and is process-global, so a node that owns it would clash
    // with any sibling node in the same process. We assume it has already run.
    room_ = std::make_unique<livekit::Room>();
    // Warning: avoid doing ROS operations in delegate callbacks
    room_->setDelegate(this);

    if (room_->connect(livekit_url, livekit_token, room_options)) {
      auto local = room_->localParticipant().lock();
      RCLCPP_INFO(this->get_logger(),
                  "Connected to LiveKit room '%s' as identity '%s'",
                  room_name_.c_str(),
                  local ? local->identity().c_str() : "(unknown)");
      Ros2CliManager::RpcTransport transport{
        [this](const std::string & id) { return hasParticipant(id); },
        [this](const std::string & id, const std::string & method,
               const std::string & payload, std::uint8_t timeout_sec) {
          return rpcPerform(id, method, payload, timeout_sec);
        },
        [this](const std::string & method, RpcHandler handler) {
          rpcRegisterMethod(method, std::move(handler));
        },
        [this](const std::string & method) { rpcUnregisterMethod(method); }};
      ros2_cli_manager_ = std::make_unique<Ros2CliManager>(
        *this,
        reentrant_callback_group_,
        std::move(transport));
    } else {
      room_.reset();
      RCLCPP_FATAL(this->get_logger(), "Failed to connect to LiveKit room.");
      return false;
    }
  }

  RCLCPP_INFO(this->get_logger(), "Creating timer for polling topics at rate %d ms",
      topic_polling_period_ms_);

  poll_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(topic_polling_period_ms_),
      std::bind(&Ros2LiveKitBridge::pollTopics, this),
      reentrant_callback_group_);

  // The bridge is considered initialized only once it is connected to a room
  // (room_ is left null when credentials are absent or connection failed).
  initialized_ = (room_ != nullptr);
  return initialized_;
}

Ros2LiveKitBridge::~Ros2LiveKitBridge()
{
  {
    std::vector<std::string> inbound_sids;
    {
      std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
      inbound_sids.reserve(inbound_data_track_states_.size());
      for (const auto & [sid, _] : inbound_data_track_states_) {
        inbound_sids.push_back(sid);
      }
    }
    for (const auto & sid : inbound_sids) {
      stopInboundDataTrack(sid);
    }
  }
  data_topic_states_.clear();
  image_topic_states_.clear();
  ros2_cli_manager_.reset();
  if (room_) {
    RCLCPP_INFO(this->get_logger(), "Disconnecting LiveKit room...");
    room_.reset();
  }
  // Note: livekit::shutdown() is intentionally NOT called here — the SDK
  // lifecycle is owned by the process entry point (see initialize()).
}

void Ros2LiveKitBridge::pollTopics()
{
  auto topic_names_and_types = this->get_topic_names_and_types();

  for (const auto &[topic_name, topic_types] : topic_names_and_types) {
    // Skip topics we have already subscribed to in this bridge instance
    if (subscriptions_.count(topic_name) > 0) {
      continue;
    }

    // Skip topics that this bridge created from inbound LiveKit tracks
    if (inbound_ros_topic_names_.count(topic_name) > 0) {
      continue;
    }

    // Only keep ROS topics that match configured ROS->LiveKit patterns
    if (!utils::matchesAnyPattern(
        topic_name, outgoing_topic_compiled_patterns_))
    {
      continue;
    }

    // Skip malformed graph entries that have no associated ROS type
    if (topic_types.empty()) {
      continue;
    }

    const auto & topic_type = topic_types.front();
    RCLCPP_INFO(this->get_logger(), "Discovered matching topic: '%s' [%s]",
                topic_name.c_str(), topic_type.c_str());
    createSubscriber(topic_name, topic_type);
  }
}

void Ros2LiveKitBridge::createSubscriber(
  const std::string & topic_name,
  const std::string & topic_type)
{
  if (topic_type == kImageMsgType) {
    createImageSubscriber(topic_name);
    // TODO(sderosa): audio track support
    // } else if (topic_type == kAudioMsgType) {
    //   createAudioSubscriber(topic_name);
  } else {
    createDataSubscriber(topic_name, topic_type);
  }
}

void Ros2LiveKitBridge::createDataSubscriber(
  const std::string & topic_name,
  const std::string & topic_type)
{
  auto qos = determineQoS(topic_name);

  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = reentrant_callback_group_;

  data_topic_states_[topic_name] = DataTopicState{};

  auto callback = [this,
      topic_name](std::shared_ptr<rclcpp::SerializedMessage> msg) {
      const auto state_it = data_topic_states_.find(topic_name);
      if (state_it == data_topic_states_.end()) {
        return;
      }
      auto & state = state_it->second;

      if (!state.track) {
        if (!room_) {
          return;
        }
        auto participant = room_->localParticipant().lock();
        if (!participant) {
          return;
        }

      // TODO: When C++ SDK supports it, input encoding type (CDR) and schema of
      // message (JSON) to this call Data track options (struct?)
        const auto publish_result = participant->publishDataTrack(topic_name);
        if (!publish_result) {
          const auto & error = publish_result.error();
          RCLCPP_ERROR(
            this->get_logger(),
            "Failed to publish data track for '%s': code=%u message=%s",
            topic_name.c_str(), static_cast<std::uint32_t>(error.code),
            error.message.c_str());
          return;
        }

        state.track = publish_result.value();
        if (!state.track) {
          RCLCPP_ERROR(this->get_logger(),
                     "publishDataTrack('%s') returned a null track",
                     topic_name.c_str());
          return;
        }

        RCLCPP_INFO(this->get_logger(), "Created data track '%s'",
                  topic_name.c_str());
      }

      auto & rcl_msg = msg->get_rcl_serialized_message();
      auto push_result = state.track->tryPush(std::vector<std::uint8_t>(
        rcl_msg.buffer, rcl_msg.buffer + rcl_msg.buffer_length));
      if (!push_result) {
        const auto & error = push_result.error();
        RCLCPP_WARN_THROTTLE(
          this->get_logger(), *this->get_clock(), 5000,
          "Failed to push data frame for '%s': code=%u message=%s",
          topic_name.c_str(), static_cast<std::uint32_t>(error.code),
          error.message.c_str());
      }
    };

  rclcpp::GenericSubscription::SharedPtr subscription;
  try {
    subscription = this->create_generic_subscription(
        topic_name, topic_type, qos, callback, sub_options);
  } catch (const std::exception & e) {
    data_topic_states_.erase(topic_name);
    RCLCPP_ERROR(this->get_logger(),
                 "Failed to create generic subscription for '%s' [%s]: %s",
                 topic_name.c_str(), topic_type.c_str(), e.what());
    return;
  } catch (...) {
    data_topic_states_.erase(topic_name);
    RCLCPP_ERROR(
        this->get_logger(),
        "Unknown exception creating generic subscription for '%s' [%s]",
        topic_name.c_str(), topic_type.c_str());
    return;
  }

  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(), "Subscribed to data topic '%s' [%s] (CDR)",
              topic_name.c_str(), topic_type.c_str());
}

void Ros2LiveKitBridge::createImageSubscriber(const std::string & topic_name)
{
  auto qos = determineQoS(topic_name);

  rclcpp::SubscriptionOptions sub_options;
  sub_options.callback_group = reentrant_callback_group_;

  image_topic_states_[topic_name] = ImageTopicState{};

  auto callback = [this,
      topic_name](sensor_msgs::msg::Image::ConstSharedPtr msg) {
      const auto state_it = image_topic_states_.find(topic_name);
      if (state_it == image_topic_states_.end()) {
        return;
      }
      auto & state = state_it->second;

      if (!state.track) {
        if (!room_) {
          return;
        }
        auto participant = room_->localParticipant().lock();
        if (!participant) {
          return;
        }

        try {
          state.source = std::make_shared<livekit::VideoSource>(
            static_cast<int>(msg->width), static_cast<int>(msg->height));
          state.track = participant->publishVideoTrack(
            topic_name, state.source, livekit::TrackSource::SOURCE_CAMERA);
          RCLCPP_INFO(this->get_logger(), "Created video track '%s' (%ux%u, %s)",
                    topic_name.c_str(), msg->width, msg->height,
                    msg->encoding.c_str());
        } catch (const std::exception & e) {
          RCLCPP_ERROR(this->get_logger(),
                     "Failed to create video track for '%s': %s",
                     topic_name.c_str(), e.what());
          return;
        }
      }

      if (!state.source) {
        RCLCPP_ERROR(this->get_logger(),
                   "Video source for '%s' is unexpectedly null",
                   topic_name.c_str());
        return;
      }

      if (state.source->width() != static_cast<int>(msg->width) ||
        state.source->height() != static_cast<int>(msg->height))
      {
        RCLCPP_WARN_THROTTLE(
          this->get_logger(), *this->get_clock(), 5000,
          "Skipping frame for '%s' because image size changed from %dx%d to "
          "%ux%u after the track was published",
          topic_name.c_str(), state.source->width(), state.source->height(),
          msg->width, msg->height);
        return;
      }

      const auto & stamp = msg->header.stamp;
      const std::int64_t timestamp_us =
        static_cast<std::int64_t>(stamp.sec) * 1'000'000 +
        static_cast<std::int64_t>(stamp.nanosec) / 1'000;

      if (msg->encoding == "rgba8" && msg->step == msg->width * 4) {
        auto frame = utils::makeRgbaVideoFrame(
          static_cast<int>(msg->width), static_cast<int>(msg->height),
          msg->data.data(), msg->data.size());
        if (!frame) {
          RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 5000,
            "Skipping RGBA image on topic '%s' because buffer size %zu does "
            "not match %ux%u geometry",
            topic_name.c_str(), msg->data.size(), msg->width, msg->height);
          return;
        }

        state.source->captureFrame(*frame, timestamp_us);
      } else {
        if (!utils::convertToRgba(*msg, state.rgba_buf)) {
          RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "Unsupported image encoding '%s' on topic '%s'",
                             msg->encoding.c_str(), topic_name.c_str());
          return;
        }
        auto frame = utils::makeRgbaVideoFrame(
          static_cast<int>(msg->width), static_cast<int>(msg->height),
          state.rgba_buf.data(), state.rgba_buf.size());
        if (!frame) {
          RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 5000,
            "Skipping converted image on topic '%s' because RGBA buffer size "
            "%zu does not match %ux%u geometry",
            topic_name.c_str(), state.rgba_buf.size(), msg->width, msg->height);
          return;
        }

        state.source->captureFrame(*frame, timestamp_us);
      }
    };

  auto subscription = this->create_subscription<sensor_msgs::msg::Image>(
      topic_name, qos, callback, sub_options);
  subscriptions_[topic_name] = subscription;

  RCLCPP_INFO(this->get_logger(), "Subscribed to image topic '%s' [%s]",
              topic_name.c_str(), kImageMsgType);
}

void Ros2LiveKitBridge::onDataTrackPublished(
  livekit::Room &,
  const livekit::DataTrackPublishedEvent & event)
{
  // TODO: Handle these various error cases below when ROS diagnostics are implemented
  if (!event.track) {
    RCLCPP_ERROR(this->get_logger(), "Received empty data track event from participant '%s'",
        event.track->publisherIdentity().c_str());
    return;
  }

  const auto & info = event.track->info();
  const auto & track_name = info.name;
  const auto normalized_track_name = utils::normalizeTrackTopicName(track_name);
  if (!normalized_track_name.has_value()) {
    RCLCPP_WARN(
      this->get_logger(),
      "Ignoring LiveKit data track from '%s' because the track name is empty",
      event.track->publisherIdentity().c_str());
    return;
  }

  // Check if the track name matches any of the incoming topic patterns
  if (!utils::matchesAnyPattern(
      *normalized_track_name,
      incoming_topic_compiled_patterns_))
  {
    RCLCPP_DEBUG(this->get_logger(),
        "Ignoring LiveKit data track '%s' from '%s' because it does not match any incoming topic patterns",
        track_name.c_str(), event.track->publisherIdentity().c_str());
    return;
  }

  const auto topic_type = liveKitToRosTopicType(track_name);
  if (!topic_type) {
    RCLCPP_WARN(
      this->get_logger(),
      "Ignoring LiveKit data track '%s' from '%s' because no "
      "configured type rule or ROS graph lookup resolved its ROS message type",
      track_name.c_str(), event.track->publisherIdentity().c_str());
    return;
  }

  const auto ros_topic_name =
    utils::liveKitToRosTopicName(event.track->publisherIdentity(), track_name);
  if (!ros_topic_name) {
    RCLCPP_WARN(
      this->get_logger(),
      "Ignoring LiveKit data track '%s' from '%s' because ROS topic name "
      "resolution failed",
      track_name.c_str(), event.track->publisherIdentity().c_str());
    return;
  }

  std::shared_ptr<InboundDataTrackState> state;
  {
    std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    if (inbound_data_track_states_.count(info.sid) > 0) {
      return;
    }

    state = std::make_shared<InboundDataTrackState>();
    state->sid = info.sid;
    state->track_name = track_name;
    state->publisher_identity = event.track->publisherIdentity();
    state->ros_topic_name = *ros_topic_name;
    state->ros_topic_type = *topic_type;
    state->publisher = this->create_generic_publisher(
      *ros_topic_name, *topic_type, rclcpp::QoS(10));

    const auto subscribe_result = event.track->subscribe();
    if (!subscribe_result) {
      const auto & error = subscribe_result.error();
      RCLCPP_ERROR(
        this->get_logger(),
        "Failed to subscribe to LiveKit data track '%s' from '%s': code=%u "
        "message=%s",
        track_name.c_str(), state->publisher_identity.c_str(),
        static_cast<std::uint32_t>(error.code), error.message.c_str());
      return;
    }

    state->stream = subscribe_result.value();
    inbound_data_track_states_[info.sid] = state;
    inbound_ros_topic_names_.insert(*ros_topic_name);
  }

  RCLCPP_INFO(
    this->get_logger(),
    "Subscribed to LiveKit data track '%s' from '%s'; publishing ROS topic "
    "'%s' [%s]",
    track_name.c_str(), state->publisher_identity.c_str(),
    ros_topic_name->c_str(), state->ros_topic_type.c_str());

  state->thread = std::thread(&Ros2LiveKitBridge::readInboundDataTrack, this, state);
}

void Ros2LiveKitBridge::onDataTrackUnpublished(
  livekit::Room &,
  const livekit::DataTrackUnpublishedEvent & event)
{
  stopInboundDataTrack(event.sid);
}

void Ros2LiveKitBridge::readInboundDataTrack(
  std::shared_ptr<InboundDataTrackState> state)
{
  livekit::DataTrackFrame frame;
  while (!state->stop.load() && state->stream && state->stream->read(frame)) {
    rclcpp::SerializedMessage serialized_msg(frame.payload.size());
    auto & rcl_msg = serialized_msg.get_rcl_serialized_message();

    if (!frame.payload.empty()) {
      std::memcpy(rcl_msg.buffer, frame.payload.data(), frame.payload.size());
    } else {
      RCLCPP_WARN(
        this->get_logger(),
        "Received empty payload from LiveKit data track '%s' from '%s'",
        state->track_name.c_str(), state->publisher_identity.c_str());
      return;
    }

    rcl_msg.buffer_length = frame.payload.size();

    try {
      state->publisher->publish(serialized_msg);
    } catch (const std::exception & e) {
      RCLCPP_WARN(
        this->get_logger(),
        "Failed to publish inbound LiveKit data frame from '%s' track '%s' to "
        "ROS topic '%s': %s",
        state->publisher_identity.c_str(), state->track_name.c_str(),
        state->ros_topic_name.c_str(), e.what());
    }
  }

  if (state->stream) {
    const auto terminal_error = state->stream->terminalError();
    if (terminal_error) {
      RCLCPP_WARN(
        this->get_logger(),
        "LiveKit data track '%s' from '%s' ended with error: code=%u message=%s",
        state->track_name.c_str(), state->publisher_identity.c_str(),
        static_cast<std::uint32_t>(terminal_error->code),
        terminal_error->message.c_str());
    }
  }
}

void Ros2LiveKitBridge::stopInboundDataTrack(const std::string & sid)
{
  std::shared_ptr<InboundDataTrackState> state;
  {
    std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    const auto it = inbound_data_track_states_.find(sid);
    if (it == inbound_data_track_states_.end()) {
      return;
    }
    state = it->second;
    inbound_data_track_states_.erase(it);
    inbound_ros_topic_names_.erase(state->ros_topic_name);
  }

  state->stop.store(true);
  if (state->stream) {
    state->stream->close();
  }
  if (state->thread.joinable()) {
    state->thread.join();
  }

  RCLCPP_INFO(
    this->get_logger(),
    "Stopped LiveKit-to-ROS data track '%s' from '%s' on ROS topic '%s'",
    state->track_name.c_str(), state->publisher_identity.c_str(),
    state->ros_topic_name.c_str());
}

/** Helpers **/

std::optional<std::string> Ros2LiveKitBridge::liveKitToRosTopicType(
  const std::string & track_name) const
{
  const auto normalized_track_name =
    utils::normalizeTrackTopicName(track_name);
  if (!normalized_track_name.has_value()) {
    return std::nullopt;
  }
  // Infer inbound type from local ROS graph only.
  const auto topics = this->get_topic_names_and_types();
  const auto topic_it = topics.find(*normalized_track_name);
  if (topic_it == topics.end() || topic_it->second.empty()) {
    return std::nullopt;
  }

  if (topic_it->second.size() > 1U) {
    RCLCPP_WARN(
      this->get_logger(),
      "Inbound track '%s' matched topic '%s' with multiple ROS types; using "
      "first discovered type '%s'.",
      track_name.c_str(), normalized_track_name->c_str(),
      topic_it->second.front().c_str());
  }
  return topic_it->second.front();
}

rclcpp::QoS
Ros2LiveKitBridge::determineQoS(const std::string & topic_name) const
{
  // Follows the approach used by ros2 topic echo and the Foxglove bridge:
  // https://github.com/foxglove/foxglove-sdk/blob/main/ros/src/foxglove_bridge/src/ros2_foxglove_bridge.cpp
  size_t depth = 0;
  size_t reliable_count = 0;
  size_t transient_local_count = 0;

  const auto publisher_info = this->get_publishers_info_by_topic(topic_name);

  for (const auto & publisher : publisher_info) {
    const auto & qos = publisher.qos_profile();

    if (qos.reliability() == rclcpp::ReliabilityPolicy::Reliable) {
      ++reliable_count;
    }
    if (qos.durability() == rclcpp::DurabilityPolicy::TransientLocal) {
      ++transient_local_count;
    }

    // Some RMW implementations report history depth as 0; use a floor of 1 per
    // publisher so the total depth is at least equal to the publisher count
    // (important for multiple transient_local publishers, e.g. several
    // tf_static broadcasters).
    const size_t pub_depth = std::max(static_cast<size_t>(1), qos.depth());
    depth += pub_depth;
  }

  depth = std::max(depth, min_qos_depth_);
  if (depth > max_qos_depth_) {
    RCLCPP_WARN(this->get_logger(),
                "Clamping history depth for topic '%s' to %zu (was %zu). "
                "Increase max_qos_depth if needed.",
                topic_name.c_str(), max_qos_depth_, depth);
    depth = max_qos_depth_;
  }

  rclcpp::QoS qos{rclcpp::KeepLast(depth)};

  // Reliability: force best-effort if topic matches the override list,
  // otherwise use RELIABLE only when every publisher offers it (mixed policies
  // fall back to best-effort so we can connect to all publishers).
  if (utils::matchesAnyPattern(topic_name,
                                      best_effort_qos_topic_patterns_))
  {
    qos.best_effort();
  } else if (!publisher_info.empty() &&
    reliable_count == publisher_info.size())
  {
    qos.reliable();
  } else {
    if (reliable_count > 0) {
      RCLCPP_WARN(this->get_logger(),
                  "Mixed reliability on topic '%s' (%zu/%zu reliable). "
                  "Falling back to BEST_EFFORT to connect to all publishers.",
                  topic_name.c_str(), reliable_count, publisher_info.size());
    }
    qos.best_effort();
  }

  // Durability: TRANSIENT_LOCAL only when every publisher offers it.
  if (!publisher_info.empty() &&
    transient_local_count == publisher_info.size())
  {
    qos.transient_local();
  } else {
    if (transient_local_count > 0) {
      RCLCPP_WARN(this->get_logger(),
                  "Mixed durability on topic '%s' (%zu/%zu transient_local). "
                  "Falling back to VOLATILE to connect to all publishers.",
                  topic_name.c_str(), transient_local_count,
                  publisher_info.size());
    }
    qos.durability_volatile();
  }

  RCLCPP_INFO(
      this->get_logger(),
      "QoS for '%s': depth=%zu, reliability=%s, durability=%s (%zu publishers)",
      topic_name.c_str(), depth,
      qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "RELIABLE" :
                                                                 "BEST_EFFORT",
      qos.durability() == rclcpp::DurabilityPolicy::TransientLocal ?
            "TRANSIENT_LOCAL" :
            "VOLATILE",
      publisher_info.size());

  return qos;
}

bool Ros2LiveKitBridge::hasParticipant(
  const std::string & participant_id) const
{
  if (!room_) {
    return false;
  }
  return static_cast<bool>(room_->remoteParticipant(participant_id).lock());
}

std::string Ros2LiveKitBridge::rpcPerform(
  const std::string & participant_id, const std::string & method,
  const std::string & payload, std::uint8_t timeout_sec)
{
  const auto local_participant =
    room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    throw std::runtime_error("LiveKit local participant is unavailable");
  }

  try {
    return local_participant->performRpc(participant_id, method, payload,
                                         static_cast<double>(timeout_sec));
  } catch (const livekit::RpcError & error) {
    // Translate LiveKit-specific errors into a plain std::exception so the
    // CLI manager stays free of any LiveKit dependency. The numeric code is
    // logged here, the LiveKit-aware layer, before it is discarded.
    RCLCPP_ERROR(
      this->get_logger(),
      "LiveKit RPC '%s' to participant '%s' failed: code=%u message=%s",
      method.c_str(), participant_id.c_str(), error.code(),
      error.message().c_str());
    throw std::runtime_error(error.message());
  }
}

void Ros2LiveKitBridge::rpcRegisterMethod(
  const std::string & method, RpcHandler handler)
{
  const auto local_participant =
    room_ ? room_->localParticipant().lock() : nullptr;
  if (!local_participant) {
    throw std::runtime_error("LiveKit local participant is unavailable");
  }

  local_participant->registerRpcMethod(
    method,
    [handler = std::move(handler)](const livekit::RpcInvocationData & data)
      -> std::optional<std::string> {return handler(data.payload);});
}

void Ros2LiveKitBridge::rpcUnregisterMethod(const std::string & method)
{
  const auto local_participant =
    room_ ? room_->localParticipant().lock() : nullptr;
  if (local_participant) {
    local_participant->unregisterRpcMethod(method);
  }
}

} // namespace ros2_livekit_bridge
