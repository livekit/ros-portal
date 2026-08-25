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

#include "ros_portal/topic_forwarder.hpp"

#include <livekit/data_track_stream.h>
#include <livekit/remote_data_track.h>

#include <algorithm>
#include <cstring>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <exception>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros_portal/introspection/introspection_utils.hpp"
#include "ros_portal/utils/generic_subscription.hpp"
#include "ros_portal/utils/image_conversion.hpp"
#include "ros_portal/utils/ros_utils.hpp"
#include "ros_portal/utils/topic_matcher.hpp"

namespace ros_portal {

namespace {
constexpr char kTopicForwarderDiagnosticTaskName[] = "topic_forwarder";

class ReaderAliveGuard {
public:
  explicit ReaderAliveGuard(std::atomic_bool& alive) : alive_(alive) { alive_.store(true, std::memory_order_relaxed); }
  ~ReaderAliveGuard() { alive_.store(false, std::memory_order_relaxed); }

  ReaderAliveGuard(const ReaderAliveGuard&) = delete;
  ReaderAliveGuard& operator=(const ReaderAliveGuard&) = delete;

private:
  std::atomic_bool& alive_;
};
} // namespace

TopicForwarder::RemoteDataTrackDescriptor TopicForwarder::createRemoteDataTrackDescriptor(
    std::shared_ptr<livekit::RemoteDataTrack> track) {
  const auto& info = track->info();
  return {
      info.sid,
      info.name,
      track->publisherIdentity(),
      info.schema,
      info.frame_encoding,
      [track =
           std::move(track)]() -> livekit::Result<std::shared_ptr<TopicForwarder::RemoteDataTrackStream>, std::string> {
        const auto subscribe_result = track->subscribe();
        if (!subscribe_result) {
          const auto& error = subscribe_result.error();
          return livekit::Result<std::shared_ptr<TopicForwarder::RemoteDataTrackStream>, std::string>::failure(
              "code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + " message=" + error.message);
        }

        const auto& livekit_stream = subscribe_result.value();
        auto stream = std::make_shared<TopicForwarder::RemoteDataTrackStream>();
        // Forward read() to the underlying LiveKit stream.
        stream->read = [livekit_stream](livekit::DataTrackFrame& frame) { return livekit_stream->read(frame); };
        // Forward close() to tear down the LiveKit stream.
        stream->close = [livekit_stream]() { livekit_stream->close(); };
        // Map terminalError() into our optional string representation.
        stream->terminal_error = [livekit_stream]() -> std::optional<std::string> {
          const auto terminal_error = livekit_stream->terminalError();
          if (!terminal_error) {
            return std::nullopt;
          }
          return "code=" + std::to_string(static_cast<std::uint32_t>(terminal_error->code)) +
                 " message=" + terminal_error->message;
        };
        return livekit::Result<std::shared_ptr<TopicForwarder::RemoteDataTrackStream>, std::string>::success(
            std::move(stream));
      },
  };
}

TopicForwarder::TopicForwarder(Options options, rclcpp::Node::WeakPtr node, LiveKitMethods livekit_methods,
                               diagnostics::DiagnosticsManagerFns diagnostics)
    : options_(std::move(options)),
      node_(std::move(node)),
      schema_manager_(std::move(livekit_methods.schema)),
      livekit_methods_(std::move(livekit_methods)),
      diagnostics_(std::move(diagnostics)),
      logger_(rclcpp::get_logger("topic_forwarder")) {
  const auto locked_node = node_.lock();
  if (!locked_node) {
    throw std::invalid_argument("TopicForwarder requires a non-expired ROS node");
  }

  if (!livekit_methods_.is_room_available || !livekit_methods_.publish_data_track ||
      !livekit_methods_.publish_video_track) {
    throw std::invalid_argument("TopicForwarder requires fully populated LiveKitMethods");
  }

  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("TopicForwarder requires fully populated DiagnosticsManagerFns");
  }

  logger_ = locked_node->get_logger().get_child("topic_forwarder");
  clock_ = locked_node->get_clock();
  callback_group_ = locked_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  diagnostics_.add(kTopicForwarderDiagnosticTaskName,
                   [this](diagnostic_updater::DiagnosticStatusWrapper& status) { populateStatus(status); });
}

TopicForwarder::~TopicForwarder() {
  diagnostics_.remove(kTopicForwarderDiagnosticTaskName);
  stopAllInboundDataTracks();
  const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
  subscriptions_.clear();
  data_topic_states_.clear();
  image_topic_states_.clear();
}

bool TopicForwarder::needsGraphDiscovery() const { return !options_.outgoing_topic_patterns.empty(); }

void TopicForwarder::reconcileTopics(const TopicNamesAndTypes& topic_names_and_types) {
  const auto now = std::chrono::steady_clock::now();
  {
    const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
    for (auto& [topic_name, subscription] : subscriptions_) {
      (void)topic_name;
      if (!subscription.handle) {
        continue;
      }
      if (subscription.handle->get_publisher_count() == 0U) {
        if (!subscription.publishers_absent_since.has_value()) {
          subscription.publishers_absent_since = now;
        }
      } else {
        subscription.publishers_absent_since.reset();
      }
    }
  }

  for (const auto& [topic_name, topic_types] : topic_names_and_types) {
    {
      const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
      if (subscriptions_.count(topic_name) > 0) {
        continue;
      }
    }

    if (!utils::matchesAnyPattern(topic_name, options_.outgoing_topic_patterns)) {
      continue;
    }

    if (topic_types.empty()) {
      continue;
    }

    const auto& topic_type = topic_types.front();
    RCLCPP_INFO(logger_, "Discovered matching topic: '%s' [%s]", topic_name.c_str(), topic_type.c_str());
    createSubscriber(topic_name, topic_type);
  }
}

bool TopicForwarder::reapExpiredSubscriptions() {
  const auto now = std::chrono::steady_clock::now();
  bool removed = false;
  const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
  for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
    auto& subscription = it->second;
    if (!subscription.publishers_absent_since.has_value()) {
      ++it;
      continue;
    }
    if (subscription.handle && subscription.handle->get_publisher_count() > 0U) {
      subscription.publishers_absent_since.reset();
      ++it;
      continue;
    }
    if ((now - *subscription.publishers_absent_since) < options_.inactive_subscription_grace) {
      ++it;
      continue;
    }

    const auto topic_name = it->first;
    data_topic_states_.erase(topic_name);
    image_topic_states_.erase(topic_name);
    it = subscriptions_.erase(it);
    removed = true;
    RCLCPP_INFO(logger_, "Removed inactive subscription for '%s'", topic_name.c_str());
  }
  return removed;
}

std::optional<std::chrono::steady_clock::time_point> TopicForwarder::nextExpiryDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> earliest;
  const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
  for (const auto& [topic_name, subscription] : subscriptions_) {
    (void)topic_name;
    if (!subscription.publishers_absent_since.has_value()) {
      continue;
    }
    const auto deadline = *subscription.publishers_absent_since + options_.inactive_subscription_grace;
    if (!earliest.has_value() || deadline < *earliest) {
      earliest = deadline;
    }
  }
  return earliest;
}

void TopicForwarder::createSubscriber(const std::string& topic_name, const std::string& topic_type) {
  if (topic_type == kImageMsgType) {
    createImageSubscriber(topic_name);
  } else {
    createDataSubscriber(topic_name, topic_type);
  }
}

void TopicForwarder::createDataSubscriber(const std::string& topic_name, const std::string& topic_type) {
  const auto qos = determineQoS(topic_name);
  const auto node = node_.lock();
  if (!node) {
    RCLCPP_DEBUG(logger_, "Skipping data subscription for '%s'; ROS node has been destroyed", topic_name.c_str());
    return;
  }

  auto callback = [this, topic_name, topic_type](const std::shared_ptr<rclcpp::SerializedMessage>& msg,
                                                 const rclcpp::MessageInfo& message_info) {
    if (isInboundPublication(message_info) || !livekit_methods_.is_room_available()) {
      return;
    }

    auto& rcl_msg = msg->get_rcl_serialized_message();

    std::shared_ptr<DataTrackWriter> writer;
    OutboundEncoding encoding = OutboundEncoding::Ros2Msg;
    {
      const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
      const auto state_it = data_topic_states_.find(topic_name);
      if (state_it == data_topic_states_.end()) {
        return;
      }
      auto& state = state_it->second;

      // Rate cap: mirror ros-tooling/topic_tools `throttle messages`. A sample
      // is forwarded only once at least one period has elapsed since the last
      // forwarded sample; samples arriving sooner are dropped on arrival.
      if (state.max_rate_hz.has_value()) {
        const auto now = clock_->now();
        if (state.last_forward_time.has_value()) {
          // Reset the window on a backward clock jump (e.g. a sim-time reset) so
          // the cap does not stall until the old timestamp is reached again.
          if (*state.last_forward_time > now) {
            RCLCPP_WARN(logger_, "Detected jump back in time for '%s'; resetting rate-cap window", topic_name.c_str());
            state.last_forward_time = now;
          }
          if ((now - *state.last_forward_time) < *state.min_period) {
            return;
          }
        }
        state.last_forward_time = now;
      }

      if (!ensureWriterLocked(topic_name, topic_type, state)) {
        return;
      }
      writer = state.writer;
      encoding = state.encoding;
    }

    livekit::Result<void, std::string> push_result = livekit::Result<void, std::string>::success();
    if (encoding == OutboundEncoding::JsonSchema) {
      std::string error;
      const auto json = introspection::jsonFromSerializedMessage(topic_type, *msg, error);
      if (!json) {
        diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
        RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Dropping frame for '%s'; JSON conversion failed: %s",
                             topic_name.c_str(), error.c_str());
        return;
      }
      push_result = writer->try_push(reinterpret_cast<const std::uint8_t*>(json->data()), json->size());
    } else {
      push_result = writer->try_push(rcl_msg.buffer, rcl_msg.buffer_length);
    }

    if (!push_result) {
      diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
      RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Failed to push data frame for '%s': %s", topic_name.c_str(),
                           push_result.error().c_str());
    }
  };

  const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
  if (subscriptions_.count(topic_name) > 0) {
    return;
  }

  DataTopicState state{};
  if (const auto rate_it = options_.outbound_rate_limits.find(topic_name);
      rate_it != options_.outbound_rate_limits.end() && rate_it->second > 0.0) {
    state.max_rate_hz = rate_it->second;
    state.min_period = rclcpp::Duration::from_seconds(1.0 / rate_it->second);
    RCLCPP_INFO(logger_, "Outbound topic '%s' rate-capped at %.3g Hz", topic_name.c_str(), rate_it->second);
  }
  if (const auto enc_it = options_.outbound_encodings.find(topic_name); enc_it != options_.outbound_encodings.end()) {
    state.encoding = enc_it->second;
  }
  const auto encoding = state.encoding;
  data_topic_states_[topic_name] = std::move(state);

  try {
    rclcpp::SubscriptionOptions sub_options;
    sub_options.callback_group = callback_group_;
    auto subscription =
        utils::createGenericSubscription(node, topic_name, topic_type, qos, std::move(callback), sub_options);
    subscriptions_[topic_name] = OutboundSubscription{std::move(subscription), std::nullopt};
  } catch (const std::exception& e) {
    data_topic_states_.erase(topic_name);
    diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Failed to create generic subscription for '%s' [%s]: %s", topic_name.c_str(),
                 topic_type.c_str(), e.what());
    return;
  } catch (...) {
    data_topic_states_.erase(topic_name);
    diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Unknown exception creating generic subscription for '%s' [%s]", topic_name.c_str(),
                 topic_type.c_str());
    return;
  }

  const char* encoding_label = encoding == OutboundEncoding::JsonSchema ? "JSON (requested jsonschema)"
                               : encoding == OutboundEncoding::Ros2Idl  ? "CDR (requested ros2idl)"
                                                                        : "CDR (requested ros2msg)";
  RCLCPP_INFO(logger_, "Subscribed to ROS topic '%s' [%s] (%s)", topic_name.c_str(), topic_type.c_str(),
              encoding_label);
}

bool TopicForwarder::ensureWriterLocked(const std::string& topic_name, const std::string& topic_type,
                                        DataTopicState& state) {
  if (state.writer) {
    return true;
  }

  if (!livekit_methods_.is_room_available()) {
    return false;
  }

  const auto schema_result = schema_manager_.ensureSchemaDefined(topic_type, state.encoding);
  if (!schema_result) {
    return false;
  }

  const auto writer_result = livekit_methods_.publish_data_track(topic_name, schema_result.value());
  if (!writer_result) {
    RCLCPP_ERROR(logger_, "Failed to publish LiveKit data track for '%s': %s", topic_name.c_str(),
                 writer_result.error().c_str());
    return false;
  }

  state.writer = writer_result.value();
  if (!state.writer || !state.writer->try_push) {
    RCLCPP_ERROR(logger_, "publish_data_track('%s') returned an invalid writer", topic_name.c_str());
    state.writer.reset();
    return false;
  }

  RCLCPP_INFO(logger_, "Created LiveKit data track '%s'", topic_name.c_str());
  return true;
}

void TopicForwarder::createImageSubscriber(const std::string& topic_name) {
  const auto qos = determineQoS(topic_name);
  const auto node = node_.lock();
  if (!node) {
    RCLCPP_DEBUG(logger_, "Skipping image subscription for '%s'; ROS node has been destroyed", topic_name.c_str());
    return;
  }

  auto callback = [this, topic_name](const sensor_msgs::msg::Image::ConstSharedPtr& msg,
                                     const rclcpp::MessageInfo& message_info) {
    if (isInboundPublication(message_info) || !livekit_methods_.is_room_available()) {
      return;
    }

    const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
    const auto state_it = image_topic_states_.find(topic_name);
    if (state_it == image_topic_states_.end()) {
      return;
    }
    auto& state = state_it->second;

    if (!state.sink) {
      if (!livekit_methods_.is_room_available()) {
        return;
      }

      const auto sink_result =
          livekit_methods_.publish_video_track(topic_name, static_cast<int>(msg->width), static_cast<int>(msg->height));
      if (!sink_result) {
        RCLCPP_ERROR(logger_, "Failed to create LiveKit video track for '%s': %s", topic_name.c_str(),
                     sink_result.error().c_str());
        return;
      }

      state.sink = sink_result.value();
      if (!state.sink || !state.sink->capture_frame) {
        RCLCPP_ERROR(logger_, "publish_video_track('%s') returned an invalid sink", topic_name.c_str());
        state.sink.reset();
        return;
      }

      RCLCPP_INFO(logger_, "Created LiveKit video track '%s' (%ux%u, %s)", topic_name.c_str(), msg->width, msg->height,
                  msg->encoding.c_str());
    }

    if (state.sink->width != static_cast<int>(msg->width) || state.sink->height != static_cast<int>(msg->height)) {
      RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000,
                           "Skipping frame for '%s' because image size changed from %dx%d to "
                           "%ux%u after the track was published",
                           topic_name.c_str(), state.sink->width, state.sink->height, msg->width, msg->height);
      return;
    }

    const auto& stamp = msg->header.stamp;
    const std::int64_t timestamp_us =
        static_cast<std::int64_t>(stamp.sec) * 1'000'000 + static_cast<std::int64_t>(stamp.nanosec) / 1'000;

    if (msg->encoding == "rgba8" && msg->step == msg->width * 4) {
      auto frame = utils::makeRgbaVideoFrame(static_cast<int>(msg->width), static_cast<int>(msg->height),
                                             msg->data.data(), msg->data.size());
      if (!frame) {
        RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000,
                             "Skipping RGBA image on topic '%s' because buffer size %zu does "
                             "not match %ux%u geometry",
                             topic_name.c_str(), msg->data.size(), msg->width, msg->height);
        return;
      }

      state.sink->capture_frame(*frame, timestamp_us);
    } else {
      if (!utils::convertToRgba(*msg, state.rgba_buf)) {
        RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Unsupported image encoding '%s' on topic '%s'",
                             msg->encoding.c_str(), topic_name.c_str());
        return;
      }
      auto frame = utils::makeRgbaVideoFrame(static_cast<int>(msg->width), static_cast<int>(msg->height),
                                             state.rgba_buf.data(), state.rgba_buf.size());
      if (!frame) {
        RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000,
                             "Skipping converted image on topic '%s' because RGBA buffer size "
                             "%zu does not match %ux%u geometry",
                             topic_name.c_str(), state.rgba_buf.size(), msg->width, msg->height);
        return;
      }

      state.sink->capture_frame(*frame, timestamp_us);
    }
  };

  const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
  if (subscriptions_.count(topic_name) > 0) {
    return;
  }

  image_topic_states_[topic_name] = ImageTopicState{};

  try {
    rclcpp::SubscriptionOptions sub_options;
    sub_options.callback_group = callback_group_;
    auto subscription =
        node->create_subscription<sensor_msgs::msg::Image>(topic_name, qos, std::move(callback), sub_options);
    subscriptions_[topic_name] = OutboundSubscription{std::move(subscription), std::nullopt};
  } catch (const std::exception& e) {
    image_topic_states_.erase(topic_name);
    RCLCPP_ERROR(logger_, "Failed to create ROS image subscription for '%s' [%s]: %s", topic_name.c_str(),
                 kImageMsgType, e.what());
    return;
  } catch (...) {
    image_topic_states_.erase(topic_name);
    diagnostic_state_.outbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_ERROR(logger_, "Unknown exception creating image subscription for '%s' [%s]", topic_name.c_str(),
                 kImageMsgType);
    return;
  }

  RCLCPP_INFO(logger_, "Subscribed to ROS image topic '%s' [%s]", topic_name.c_str(), kImageMsgType);
}

void TopicForwarder::onDataTrackPublished(std::shared_ptr<livekit::RemoteDataTrack> track) {
  if (!track) {
    RCLCPP_WARN(logger_, "Ignoring null LiveKit data track");
    return;
  }

  onDataTrackPublished(createRemoteDataTrackDescriptor(std::move(track)));
}

void TopicForwarder::onDataTrackPublished(RemoteDataTrackDescriptor descriptor) {
  if (descriptor.sid.empty()) {
    RCLCPP_WARN(logger_, "Ignoring LiveKit data track with empty SID");
    return;
  }

  const auto normalized_track_name = utils::normalizeTrackTopicName(descriptor.track_name);
  if (!normalized_track_name.has_value()) {
    RCLCPP_WARN(logger_, "Ignoring LiveKit data track from '%s' because the track name is empty",
                descriptor.publisher_identity.c_str());
    return;
  }

  if (!isIncomingTopicAllowed(*normalized_track_name)) {
    diagnostic_state_.inbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000,
                         "Ignoring LiveKit data track '%s' from '%s' because it does not match "
                         "any incoming topic patterns",
                         descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  const auto topic_type = resolveInboundRosTopicType(descriptor.track_name, descriptor.schema);
  if (!topic_type) {
    diagnostic_state_.inbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_WARN(logger_,
                "Ignoring LiveKit data track '%s' from '%s' because neither its schema "
                "nor the ROS graph resolved its ROS message type",
                descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  if (!schema_manager_.validateInboundSchema({
          descriptor.track_name,
          descriptor.publisher_identity,
          *topic_type,
          descriptor.schema,
          descriptor.frame_encoding,
      })) {
    // Return to prevent creating a publisher for the track due to invalid schema
    return;
  }

  const bool preserve_id = utils::matchesAnyPattern(*normalized_track_name, options_.preserve_id_topic_patterns);
  const auto ros_topic_name = preserve_id
                                  ? utils::liveKitToRosTopicName(descriptor.publisher_identity, descriptor.track_name)
                                  : utils::liveKitToRosTopicName(descriptor.track_name);
  if (!ros_topic_name) {
    diagnostic_state_.inbound_failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_WARN(logger_,
                "Ignoring LiveKit data track '%s' from '%s' because ROS topic name "
                "resolution failed",
                descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  if (!descriptor.subscribe) {
    RCLCPP_ERROR(logger_,
                 "Ignoring LiveKit data track '%s' from '%s' because subscribe callback "
                 "is unset",
                 descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  if (!descriptor.frame_encoding.has_value()) {
    RCLCPP_ERROR(logger_, "Ignoring LiveKit data track '%s' from '%s' because frame encoding is unset",
                 descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  // Pin the owning ROS Portal node before any lock: this may be the last reference, and
  // releasing it under inbound_data_track_states_mutex_ deadlocks in
  // ~TopicForwarder -> stopAllInboundDataTracks().
  const auto node = node_.lock();
  if (!node) {
    RCLCPP_WARN(logger_,
                "Cannot create ROS publisher for LiveKit data track '%s' from '%s'; "
                "ROS node has been destroyed",
                descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
    return;
  }

  std::shared_ptr<InboundDataTrackState> state;
  {
    const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    if (inbound_data_track_states_.count(descriptor.sid) > 0) {
      return;
    }

    state = std::make_shared<InboundDataTrackState>();
    state->sid = descriptor.sid;
    state->track_name = descriptor.track_name;
    state->publisher_identity = descriptor.publisher_identity;
    state->ros_topic_name = *ros_topic_name;
    state->ros_topic_type = *topic_type;
    state->frame_encoding = *descriptor.frame_encoding;

    try {
      state->publisher = node->create_generic_publisher(*ros_topic_name, *topic_type, rclcpp::QoS(10));
    } catch (const std::exception& e) {
      RCLCPP_ERROR(logger_,
                   "Failed to create ROS publisher for LiveKit data track '%s' from "
                   "'%s': %s",
                   descriptor.track_name.c_str(), descriptor.publisher_identity.c_str(), e.what());
      return;
    }

    if (!state->publisher) {
      RCLCPP_ERROR(logger_,
                   "Failed to create ROS publisher for LiveKit data track '%s' from "
                   "'%s': publisher handle is invalid",
                   descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
      return;
    }

    const auto subscribe_result = descriptor.subscribe();
    if (!subscribe_result) {
      RCLCPP_ERROR(logger_, "Failed to subscribe to LiveKit data track '%s' from '%s': %s",
                   descriptor.track_name.c_str(), descriptor.publisher_identity.c_str(),
                   subscribe_result.error().c_str());
      return;
    }

    state->stream = subscribe_result.value();
    if (!state->stream || !state->stream->read || !state->stream->close) {
      RCLCPP_ERROR(logger_,
                   "Failed to subscribe to LiveKit data track '%s' from '%s': stream "
                   "handle is invalid",
                   descriptor.track_name.c_str(), descriptor.publisher_identity.c_str());
      return;
    }

    inbound_data_track_states_[descriptor.sid] = state;
  }

  RCLCPP_INFO(logger_,
              "Subscribed to LiveKit data track '%s' from '%s'; publishing ROS topic "
              "'%s' [%s]",
              descriptor.track_name.c_str(), descriptor.publisher_identity.c_str(), ros_topic_name->c_str(),
              state->ros_topic_type.c_str());

  try {
    state->thread = std::thread(&TopicForwarder::readInboundDataTrack, this, state);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(logger_,
                 "Failed to start inbound data track reader thread for '%s' from '%s': "
                 "%s",
                 descriptor.track_name.c_str(), descriptor.publisher_identity.c_str(), e.what());
    state->stop.store(true);
    if (state->stream && state->stream->close) {
      state->stream->close();
    }
    {
      const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
      inbound_data_track_states_.erase(descriptor.sid);
    }
  }
}

void TopicForwarder::onDataTrackUnpublished(const std::string& sid) {
  std::shared_ptr<InboundDataTrackState> state;
  {
    const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    const auto it = inbound_data_track_states_.find(sid);
    if (it == inbound_data_track_states_.end()) {
      return;
    }
    state = it->second;
    inbound_data_track_states_.erase(it);
  }

  state->stop.store(true);
  if (state->stream && state->stream->close) {
    state->stream->close();
  }
  if (state->thread.joinable()) {
    state->thread.join();
  }

  RCLCPP_INFO(logger_, "Stopped LiveKit-to-ROS data track '%s' from '%s' on ROS topic '%s'", state->track_name.c_str(),
              state->publisher_identity.c_str(), state->ros_topic_name.c_str());
}

bool TopicForwarder::isIncomingTopicAllowed(const std::string& topic_name) const {
  return utils::matchesAnyPattern(topic_name, options_.incoming_topic_patterns);
}

bool TopicForwarder::isInboundPublication(const rclcpp::MessageInfo& message_info) {
  const auto& publisher_gid = message_info.get_rmw_message_info().publisher_gid;
  const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
  return std::any_of(inbound_data_track_states_.begin(), inbound_data_track_states_.end(), [&](const auto& entry) {
    const auto& state = entry.second;
    return state && state->publisher && *state->publisher == publisher_gid;
  });
}

std::optional<std::string> TopicForwarder::resolveInboundRosTopicType(
    const std::string& track_name, const std::optional<livekit::DataTrackSchemaId>& schema) const {
  const auto normalized_track_name = utils::normalizeTrackTopicName(track_name);
  if (!normalized_track_name.has_value()) {
    return std::nullopt;
  }

  TopicGraphSnapshot topics;
  if (options_.topic_snapshot) {
    topics = options_.topic_snapshot();
  }
  if (!topics) {
    const auto node = node_.lock();
    if (!node) {
      return std::nullopt;
    }
    topics = std::make_shared<const TopicNamesAndTypes>(node->get_topic_names_and_types());
  }

  const auto topic_it = topics->find(*normalized_track_name);
  if (topic_it == topics->end() || topic_it->second.empty()) {
    if (schema.has_value() && !schema->name.empty()) {
      RCLCPP_INFO(logger_,
                  "Inbound track '%s' has no local ROS endpoint yet; using advertised "
                  "schema type '%s' for exact local validation",
                  track_name.c_str(), schema->name.c_str());
      return schema->name;
    }
    return std::nullopt;
  }

  if (topic_it->second.size() > 1U) {
    if (schema.has_value() &&
        std::find(topic_it->second.begin(), topic_it->second.end(), schema->name) != topic_it->second.end()) {
      RCLCPP_WARN(logger_,
                  "Inbound track '%s' matched topic '%s' with multiple ROS types; using "
                  "advertised schema type '%s'.",
                  track_name.c_str(), normalized_track_name->c_str(), schema->name.c_str());
      return schema->name;
    }
    RCLCPP_WARN(logger_,
                "Inbound track '%s' matched topic '%s' with multiple ROS types; using "
                "first discovered type '%s'.",
                track_name.c_str(), normalized_track_name->c_str(), topic_it->second.front().c_str());
  }
  return topic_it->second.front();
}

rclcpp::QoS TopicForwarder::determineQoS(const std::string& topic_name) const {
  size_t depth = 0;
  size_t reliable_count = 0;
  size_t transient_local_count = 0;

  std::vector<rclcpp::TopicEndpointInfo> publisher_info;
  {
    const auto node = node_.lock();
    if (!node) {
      RCLCPP_WARN(logger_,
                  "Cannot inspect publishers for '%s' because the ROS node has been "
                  "destroyed; using minimum-depth best-effort QoS",
                  topic_name.c_str());
      return rclcpp::QoS(rclcpp::KeepLast(options_.min_qos_depth)).best_effort();
    }
    publisher_info = node->get_publishers_info_by_topic(topic_name);
  }

  for (const auto& publisher : publisher_info) {
    const auto& qos = publisher.qos_profile();

    if (qos.reliability() == rclcpp::ReliabilityPolicy::Reliable) {
      ++reliable_count;
    }
    if (qos.durability() == rclcpp::DurabilityPolicy::TransientLocal) {
      ++transient_local_count;
    }

    const size_t pub_depth = std::max(static_cast<size_t>(1), qos.depth());
    depth += pub_depth;
  }

  depth = std::max(depth, options_.min_qos_depth);
  if (depth > options_.max_qos_depth) {
    RCLCPP_WARN(logger_,
                "Clamping history depth for topic '%s' to %zu (was %zu). Increase "
                "max_qos_depth if needed.",
                topic_name.c_str(), options_.max_qos_depth, depth);
    depth = options_.max_qos_depth;
  }

  rclcpp::QoS qos{rclcpp::KeepLast(depth)};

  if (utils::matchesAnyPattern(topic_name, options_.best_effort_qos_topic_patterns)) {
    qos.best_effort();
  } else if (!publisher_info.empty() && reliable_count == publisher_info.size()) {
    qos.reliable();
  } else {
    if (reliable_count > 0) {
      RCLCPP_WARN(logger_,
                  "Mixed reliability on topic '%s' (%zu/%zu reliable). Falling back to "
                  "BEST_EFFORT to connect to all publishers.",
                  topic_name.c_str(), reliable_count, publisher_info.size());
    }
    qos.best_effort();
  }

  if (!publisher_info.empty() && transient_local_count == publisher_info.size()) {
    qos.transient_local();
  } else {
    if (transient_local_count > 0) {
      RCLCPP_WARN(logger_,
                  "Mixed durability on topic '%s' (%zu/%zu transient_local). Falling "
                  "back to VOLATILE to connect to all publishers.",
                  topic_name.c_str(), transient_local_count, publisher_info.size());
    }
    qos.durability_volatile();
  }

  RCLCPP_INFO(logger_, "QoS for '%s': depth=%zu, reliability=%s, durability=%s (%zu publishers)", topic_name.c_str(),
              depth, qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "RELIABLE" : "BEST_EFFORT",
              qos.durability() == rclcpp::DurabilityPolicy::TransientLocal ? "TRANSIENT_LOCAL" : "VOLATILE",
              publisher_info.size());

  return qos;
}

void TopicForwarder::readInboundDataTrack(std::shared_ptr<InboundDataTrackState> state) {
  const ReaderAliveGuard reader_alive(state->reader_thread_alive);
  livekit::DataTrackFrame frame;
  while (!state->stop.load() && state->stream && state->stream->read && state->stream->read(frame)) {
    if (!livekit_methods_.is_room_available()) {
      continue;
    }

    std::optional<rclcpp::SerializedMessage> serialized_msg;
    if (state->frame_encoding == livekit::DataTrackFrameEncoding::Json) {
      std::string error;
      serialized_msg = introspection::serializedMessageFromJson(
          state->ros_topic_type, std::string(frame.payload.begin(), frame.payload.end()), error);
      if (!serialized_msg) {
        diagnostic_state_.inbound_json_decode_failures.fetch_add(1, std::memory_order_relaxed);
        RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000,
                             "Dropping invalid JSON frame from LiveKit data track '%s' from '%s': %s",
                             state->track_name.c_str(), state->publisher_identity.c_str(), error.c_str());
        continue;
      }
    } else {
      serialized_msg.emplace(frame.payload.size());
      auto& rcl_msg = serialized_msg->get_rcl_serialized_message();
      if (!frame.payload.empty()) {
        std::memcpy(rcl_msg.buffer, frame.payload.data(), frame.payload.size());
      } else {
        diagnostic_state_.inbound_empty_payload_drops.fetch_add(1, std::memory_order_relaxed);
        RCLCPP_WARN(logger_, "Received empty payload from LiveKit data track '%s' from '%s'", state->track_name.c_str(),
                    state->publisher_identity.c_str());
        return;
      }
      rcl_msg.buffer_length = frame.payload.size();
    }

    try {
      state->publisher->publish(*serialized_msg);
    } catch (const std::exception& e) {
      diagnostic_state_.inbound_failures.fetch_add(1, std::memory_order_relaxed);
      RCLCPP_WARN(logger_,
                  "Failed to publish inbound LiveKit data frame from '%s' "
                  "track '%s' to "
                  "ROS topic '%s': %s",
                  state->publisher_identity.c_str(), state->track_name.c_str(), state->ros_topic_name.c_str(),
                  e.what());
    }
  }

  if (state->stream && state->stream->terminal_error) {
    const auto terminal_error = state->stream->terminal_error();
    if (terminal_error) {
      {
        const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
        diagnostic_state_.inbound_last_terminal_error = *terminal_error;
      }
      diagnostic_state_.inbound_terminal_errors.fetch_add(1, std::memory_order_relaxed);
      RCLCPP_WARN(logger_, "LiveKit data track '%s' from '%s' ended with error: %s", state->track_name.c_str(),
                  state->publisher_identity.c_str(), terminal_error->c_str());
    }
  }
}

void TopicForwarder::stopAllInboundDataTracks() {
  std::vector<std::string> inbound_sids;
  {
    const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    inbound_sids.reserve(inbound_data_track_states_.size());
    for (const auto& [sid, _] : inbound_data_track_states_) {
      inbound_sids.push_back(sid);
    }
  }

  for (const auto& sid : inbound_sids) {
    onDataTrackUnpublished(sid);
  }
}

void TopicForwarder::populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) {
  // Schema failures are reported through the outbound and inbound failure counters rather
  // than as their own fields. Each one is logged in detail by the schema manager.
  const auto schema_diagnostics = schema_manager_.diagnosticsSnapshot();
  const auto schema_outbound_failures = schema_diagnostics.define_failures + schema_diagnostics.render_failures +
                                        schema_diagnostics.encoding_mismatch_skips;
  const auto schema_inbound_rejections =
      schema_diagnostics.inbound_rejected_no_encoding + schema_diagnostics.inbound_rejected_name_mismatch +
      schema_diagnostics.inbound_rejected_remote_unavailable + schema_diagnostics.inbound_rejected_definition_differs;

  const auto outbound_failures =
      diagnostic_state_.outbound_failures.load(std::memory_order_relaxed) + schema_outbound_failures;
  const auto inbound_failures =
      diagnostic_state_.inbound_failures.load(std::memory_order_relaxed) + schema_inbound_rejections;
  const auto inbound_json_decode_failures =
      diagnostic_state_.inbound_json_decode_failures.load(std::memory_order_relaxed);
  const auto inbound_empty_payload_drops =
      diagnostic_state_.inbound_empty_payload_drops.load(std::memory_order_relaxed);
  const auto inbound_terminal_errors = diagnostic_state_.inbound_terminal_errors.load(std::memory_order_relaxed);

  std::size_t outbound_data_tracks = 0U;
  std::size_t outbound_video_tracks = 0U;
  std::size_t outbound_subscriptions = 0U;
  std::size_t outbound_data_tracks_pending_writer = 0U;
  {
    const std::lock_guard<std::mutex> lock(outbound_topics_mutex_);
    outbound_data_tracks = data_topic_states_.size();
    outbound_video_tracks = image_topic_states_.size();
    outbound_subscriptions = subscriptions_.size();
    outbound_data_tracks_pending_writer =
        static_cast<std::size_t>(std::count_if(data_topic_states_.begin(), data_topic_states_.end(),
                                               [](const auto& entry) { return entry.second.writer == nullptr; }));
  }

  std::size_t inbound_data_tracks = 0U;
  std::size_t inbound_reader_threads_alive = 0U;
  std::string inbound_last_terminal_error;
  {
    const std::lock_guard<std::mutex> lock(inbound_data_track_states_mutex_);
    inbound_data_tracks = inbound_data_track_states_.size();
    inbound_reader_threads_alive = static_cast<std::size_t>(std::count_if(
        inbound_data_track_states_.begin(), inbound_data_track_states_.end(),
        [](const auto& entry) { return entry.second->reader_thread_alive.load(std::memory_order_relaxed); }));
    inbound_last_terminal_error = diagnostic_state_.inbound_last_terminal_error;
  }

  // Pending writers and stopped reader threads are not published as fields; they only
  // raise the summary to ERROR, which names the unavailable forwarding path.
  const bool forwarding_unavailable =
      outbound_data_tracks_pending_writer > 0U || inbound_reader_threads_alive < inbound_data_tracks;
  const bool failures_detected = outbound_failures > 0U || inbound_failures > 0U || inbound_json_decode_failures > 0U ||
                                 inbound_empty_payload_drops > 0U || inbound_terminal_errors > 0U;

  if (forwarding_unavailable) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "One or more forwarding paths are unavailable");
  } else if (failures_detected) {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Topic forwarding failures or drops detected");
  } else {
    status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Topic forwarding healthy");
  }

  status.add("outbound.data_tracks", outbound_data_tracks);
  status.add("outbound.video_tracks", outbound_video_tracks);
  status.add("outbound.subscriptions", outbound_subscriptions);
  status.add("outbound.failures", outbound_failures);
  status.add("inbound.data_tracks", inbound_data_tracks);
  status.add("inbound.failures", inbound_failures);
  status.add("inbound.json_decode_failures", inbound_json_decode_failures);
  status.add("inbound.empty_payload_drops", inbound_empty_payload_drops);
  status.add("inbound.terminal_errors", inbound_terminal_errors);
  status.add("inbound.last_terminal_error", inbound_last_terminal_error.empty() ? "none" : inbound_last_terminal_error);
}

} // namespace ros_portal
