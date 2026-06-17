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
#include "ros2_livekit_bridge/utils/image_conversion.hpp"
#include "ros2_livekit_bridge/utils/ros_utils.hpp"
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"
#include "ros2_livekit_bridge/utils/track_transport_selector.hpp"

#include <livekit/livekit.h>

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

void Ros2LiveKitBridge::initializeDataTrackManager()
{
  managers::DataTrackTopicManager::Dependencies dependencies;
  dependencies.node = this;
  dependencies.room_accessor = [this]() -> livekit::Room * {
      return room_.get();
    };
  dependencies.topic_routes = &topic_routes_;
  dependencies.callback_group = reentrant_callback_group_;
  dependencies.qos_for_topic = [this](const std::string & topic_name) {
      return determineQoS(topic_name);
    };
  dependencies.resolve_ros_type = [this](const std::string & track_name) {
      return liveKitToRosTopicType(track_name);
    };

  data_track_manager_ =
    std::make_unique<managers::DataTrackTopicManager>(std::move(dependencies));
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
  topic_routes_ = utils::compileTopicRoutes(*config);
  utils::logPatternCompileErrors(topic_routes_.errors, this->get_logger());

  std::vector<utils::PatternCompileError> pattern_errors;
  auto best_effort_topics =
    this->get_parameter("best_effort_qos_topics").as_string_array();
  best_effort_qos_topic_patterns_ =
    utils::compileRegexPatterns(best_effort_topics, &pattern_errors);
  utils::logPatternCompileErrors(pattern_errors, this->get_logger());

  initializeDataTrackManager();

  RCLCPP_INFO(this->get_logger(),
              "Room: '%s', polling period: %d ms, watching %zu ROS topic "
              "patterns, %zu LiveKit-to-ROS topic patterns, QoS depth range: "
              "[%zu, %zu]",
              room_name_.c_str(), topic_polling_period_ms_,
              topic_routes_.outgoing.size(),
              topic_routes_.incoming.size(),
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
    livekit::initialize(livekit::LogLevel::Info);
    sdk_initialized_ = true;

    room_ = std::make_unique<livekit::Room>();
    // Warning: avoid doing ROS operations in delegate callbacks
    room_->setDelegate(this);

    if (room_->connect(livekit_url, livekit_token, room_options)) {
      RCLCPP_INFO(this->get_logger(), "Connected to LiveKit room: '%s'", room_name_.c_str());
    } else {
      room_.reset();
      livekit::shutdown();
      sdk_initialized_ = false;
      RCLCPP_FATAL(this->get_logger(), "Failed to connect to LiveKit room.");
      return false;
    }
  }

  RCLCPP_INFO(this->get_logger(), "Creating timer for polling topics at rate %d ms", topic_polling_period_ms_);

  poll_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(topic_polling_period_ms_),
      std::bind(&Ros2LiveKitBridge::pollTopics, this),
      reentrant_callback_group_);

  initialized_ = true && sdk_initialized_;
  return initialized_;
}

Ros2LiveKitBridge::~Ros2LiveKitBridge()
{
  if (data_track_manager_) {
    data_track_manager_->shutdown();
    data_track_manager_.reset();
  }
  image_topic_states_.clear();
  if (room_) {
    RCLCPP_INFO(this->get_logger(), "Disconnecting LiveKit room...");
    room_.reset();
  }
  if (sdk_initialized_) {
    livekit::shutdown();
    sdk_initialized_ = false;
  }
}

void Ros2LiveKitBridge::pollTopics()
{
  if (!data_track_manager_) {
    return;
  }

  auto topic_names_and_types = this->get_topic_names_and_types();

  for (const auto &[topic_name, topic_types] : topic_names_and_types) {
    // Skip topics we have already subscribed to in this bridge instance
    if (subscriptions_.count(topic_name) > 0 ||
      data_track_manager_->hasOutboundSubscription(topic_name))
    {
      continue;
    }

    // Skip topics that this bridge created from inbound LiveKit tracks
    if (data_track_manager_->isInboundManagedRosTopic(topic_name)) {
      continue;
    }

    // Only keep ROS topics that match configured ROS->LiveKit patterns
    if (!data_track_manager_->matchesOutboundRoute(topic_name)) {
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
  switch (utils::selectTrackTransport(topic_type)) {
    case utils::TrackTransport::Video:
      createImageSubscriber(topic_name);
      break;
    case utils::TrackTransport::Audio:
      // TODO: createAudioSubscriber(topic_name) once AudioTrackTopicManager exists.
      RCLCPP_WARN(
        this->get_logger(),
        "Audio transport is not implemented yet; ignoring topic '%s' [%s]",
        topic_name.c_str(), topic_type.c_str());
      break;
    case utils::TrackTransport::Data:
      data_track_manager_->createOutboundSubscriber(topic_name, topic_type);
      break;
  }
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
  if (data_track_manager_) {
    data_track_manager_->onDataTrackPublished(event);
  }
}

void Ros2LiveKitBridge::onDataTrackUnpublished(
  livekit::Room &,
  const livekit::DataTrackUnpublishedEvent & event)
{
  if (data_track_manager_) {
    data_track_manager_->onDataTrackUnpublished(event.sid);
  }
}

/** Helpers **/

std::optional<std::string> Ros2LiveKitBridge::liveKitToRosTopicType(
  const std::string & track_name) const
{
  const std::string normalized_track_name =
    utils::normalizeTrackTopicName(track_name);
  // Infer inbound type from local ROS graph only.
  const auto topics = this->get_topic_names_and_types();
  const auto topic_it = topics.find(normalized_track_name);
  if (topic_it == topics.end() || topic_it->second.empty()) {
    return std::nullopt;
  }

  if (topic_it->second.size() > 1U) {
    RCLCPP_WARN(
      this->get_logger(),
      "Inbound track '%s' matched topic '%s' with multiple ROS types; using "
      "first discovered type '%s'.",
      track_name.c_str(), normalized_track_name.c_str(),
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

} // namespace ros2_livekit_bridge
