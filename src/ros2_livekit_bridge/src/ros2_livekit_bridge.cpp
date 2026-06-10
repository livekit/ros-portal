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
#include "ros2_livekit_bridge/utils/topic_matcher.hpp"
#include "ros2_livekit_bridge_config/config/config_parser.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include <livekit/livekit.h>
#include <livekit/video_frame.h>

namespace ros2_livekit_bridge
{

namespace
{

namespace bridge_utils = ::livekit::ros_bridge::utils;
namespace bridge_config = ::ros2_livekit_bridge_config;

constexpr int DEFAULT_TOPIC_POLLING_PERIOD_MS = 500;
constexpr int DEFAULT_ROS_THREADS = 4;
constexpr size_t DEFAULT_MIN_QOS_DEPTH = 1;
constexpr size_t DEFAULT_MAX_QOS_DEPTH = 25;
constexpr const char *kImageMsgType = "sensor_msgs/msg/Image";

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(
  int width, int height,
  const std::uint8_t *rgba,
  std::size_t rgba_size)
{
  const std::size_t expected_size =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  if (rgba_size != expected_size) {
    return std::nullopt;
  }

  auto frame =
    livekit::VideoFrame::create(width, height, livekit::VideoBufferType::RGBA);
  std::memcpy(frame.data(), rgba, rgba_size);
  return frame;
}

/**
 * @brief Resolve a credential from an environment variable.
 * @param env_var_name The name of the environment variable
 * @param source The source of the credential. This is set to a
 * human-readable label of the source.
 * @return The resolved credential
 */
std::string resolveEnvironmentCredential(
  const std::string & env_var_name, std::string & source)
{
  const char *env_val = std::getenv(env_var_name.c_str());
  if (env_val && env_val[0] != '\0') {
    source = "environment variable " + env_var_name;
    return std::string(env_val);
  }
  source = "none";
  return {};
}

void logPatternCompileErrors(
  const std::vector<bridge_utils::PatternCompileError> & errors,
  rclcpp::Logger logger)
{
  for (const auto & error : errors) {
    RCLCPP_ERROR(logger, "Invalid regex pattern '%s': %s",
                 error.pattern.c_str(), error.message.c_str());
  }
}

bridge_config::BridgeConfig parseBridgeConfig(
  const std::filesystem::path & path, rclcpp::Logger logger)
{
  if (path.empty()) {
    throw std::invalid_argument(
            "config_path parameter must point to a ros2_livekit_bridge config "
            "YAML file");
  }

  try {
    return bridge_config::ConfigParser{}.parseFile(path);
  } catch (const std::exception & e) {
    RCLCPP_FATAL(logger, "Failed to parse config '%s': %s",
                 path.string().c_str(), e.what());
    throw;
  }
}

std::vector<std::string> outgoingTopicPatterns(
  const bridge_config::BridgeConfig & config)
{
  std::vector<std::string> patterns;
  patterns.reserve(config.topics.size());

  for (const auto & topic_config : config.topics) {
    if (topic_config.direction == bridge_config::Direction::Out ||
      topic_config.direction == bridge_config::Direction::Bidirectional)
    {
      patterns.push_back(topic_config.topic);
    }
  }

  return patterns;
}

} // namespace

Ros2LiveKitBridge::Ros2LiveKitBridge(const rclcpp::NodeOptions & options)
: rclcpp::Node("ros2_livekit_bridge", options)
{
  this->declare_parameter<std::string>("config_path", "");
  const std::vector<std::string> kEmptyStringVec{};
  this->declare_parameter<int>("min_qos_depth",
                               static_cast<int>(DEFAULT_MIN_QOS_DEPTH));
  this->declare_parameter<int>("max_qos_depth",
                               static_cast<int>(DEFAULT_MAX_QOS_DEPTH));
  this->declare_parameter("best_effort_qos_topics",
                          rclcpp::ParameterValue(kEmptyStringVec));

  const auto config_path = std::filesystem::path(
    this->get_parameter("config_path").as_string());
  const auto config = parseBridgeConfig(config_path, this->get_logger());

  room_name_ = config.room_name;
  topic_polling_period_ms_ =
    config.topic_polling_period_ms.value_or(DEFAULT_TOPIC_POLLING_PERIOD_MS);
  ros_threads_ = config.ros_threads.value_or(DEFAULT_ROS_THREADS);

  reentrant_callback_group_ =
    this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  min_qos_depth_ =
    static_cast<size_t>(this->get_parameter("min_qos_depth").as_int());
  max_qos_depth_ =
    static_cast<size_t>(this->get_parameter("max_qos_depth").as_int());
  ros_topic_patterns_ = outgoingTopicPatterns(config);
  std::vector<bridge_utils::PatternCompileError> pattern_errors;
  compiled_patterns_ =
    bridge_utils::compileRegexPatterns(ros_topic_patterns_, &pattern_errors);
  logPatternCompileErrors(pattern_errors, this->get_logger());

  auto best_effort_topics =
    this->get_parameter("best_effort_qos_topics").as_string_array();
  pattern_errors.clear();
  best_effort_qos_topic_patterns_ =
    bridge_utils::compileRegexPatterns(best_effort_topics, &pattern_errors);
  logPatternCompileErrors(pattern_errors, this->get_logger());

  RCLCPP_INFO(this->get_logger(),
              "Room: '%s', polling period: %d ms, watching %zu topic patterns, "
              "QoS depth range: [%zu, %zu]",
              room_name_.c_str(), topic_polling_period_ms_,
              compiled_patterns_.size(), min_qos_depth_, max_qos_depth_);

  RCLCPP_INFO(this->get_logger(), "Attempting to resolve LiveKit credentials");

  // ----- Resolve LiveKit credentials from environment variables only -----
  std::string url_source, token_source;
  const std::string livekit_url =
    resolveEnvironmentCredential("LIVEKIT_URL", url_source);
  const std::string livekit_token =
    resolveEnvironmentCredential("LIVEKIT_TOKEN", token_source);

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

    if (room_->connect(livekit_url, livekit_token, room_options)) {
      RCLCPP_INFO(this->get_logger(), "Connected to LiveKit room.");
    } else {
      room_.reset();
      livekit::shutdown();
      sdk_initialized_ = false;
      RCLCPP_ERROR(this->get_logger(), "Failed to connect to LiveKit room.");
    }
  }

  RCLCPP_INFO(this->get_logger(), "Creating timer for polling topics");

  poll_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(topic_polling_period_ms_),
      std::bind(&Ros2LiveKitBridge::pollTopics, this),
      reentrant_callback_group_);
}

Ros2LiveKitBridge::~Ros2LiveKitBridge()
{
  data_topic_states_.clear();
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
  auto topic_names_and_types = this->get_topic_names_and_types();

  for (const auto &[topic_name, topic_types] : topic_names_and_types) {
    if (subscriptions_.count(topic_name) > 0) {
      continue;
    }

    if (!matchesTopic(topic_name)) {
      continue;
    }

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

        // TODO: When C++ SDK supports it, input encoding type (CDR) and schema of message (JSON) to this call
        // Data track options (struct?)
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
        auto frame = makeRgbaVideoFrame(static_cast<int>(msg->width),
                                      static_cast<int>(msg->height),
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
        if (!bridge_utils::convertToRgba(*msg, state.rgba_buf)) {
          RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                             "Unsupported image encoding '%s' on topic '%s'",
                             msg->encoding.c_str(), topic_name.c_str());
          return;
        }

        auto frame = makeRgbaVideoFrame(static_cast<int>(msg->width),
                                      static_cast<int>(msg->height),
                                      state.rgba_buf.data(),
                                      state.rgba_buf.size());
        if (!frame) {
          RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 5000,
            "Skipping converted image on topic '%s' because RGBA buffer size "
            "%zu does not match %ux%u geometry",
            topic_name.c_str(), state.rgba_buf.size(), msg->width,
            msg->height);
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

/** Helpers **/

bool Ros2LiveKitBridge::matchesTopic(const std::string & topic_name) const
{
  return bridge_utils::matchesAnyPattern(topic_name, compiled_patterns_);
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
  if (bridge_utils::matchesAnyPattern(
      topic_name, best_effort_qos_topic_patterns_))
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
