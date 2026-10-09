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

#include "ros_portal/inbound_video_track.hpp"

#include <builtin_interfaces/msg/time.hpp>
#include <exception>
#include <limits>
#include <optional>
#include <rclcpp/logging.hpp>
#include <string_view>
#include <utility>

#include "ros_portal/utils/reader_alive_guard.hpp"
#include "ros_portal/utils/video_frame_metadata.hpp"

namespace ros_portal {

namespace {
/// @brief `sensor_msgs/CompressedImage.format` for baseline JPEG payloads.
constexpr char kJpegFormat[] = "jpeg";
constexpr std::uint64_t kMicrosecondsPerSecond = 1'000'000U;
constexpr std::uint32_t kNanosecondsPerMicrosecond = 1'000U;

/// @brief Convert a sender `user_timestamp_us` to a ROS time.
/// @return std::nullopt when the seconds do not fit `builtin_interfaces/Time`.
std::optional<builtin_interfaces::msg::Time> toRosTime(const std::uint64_t timestamp_us) {
  const std::uint64_t seconds = timestamp_us / kMicrosecondsPerSecond;
  if (seconds > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  builtin_interfaces::msg::Time stamp;
  stamp.sec = static_cast<std::int32_t>(seconds);
  stamp.nanosec = static_cast<std::uint32_t>(timestamp_us % kMicrosecondsPerSecond) * kNanosecondsPerMicrosecond;
  return stamp;
}
} // namespace

InboundVideoTrack::InboundVideoTrack(Options options, std::shared_ptr<Stream> stream,
                                     rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr publisher,
                                     rclcpp::Clock::SharedPtr clock, rclcpp::Logger logger,
                                     IsRoomAvailableFn is_room_available, Counters& counters)
    : options_(std::move(options)),
      stream_(std::move(stream)),
      publisher_(std::move(publisher)),
      clock_(std::move(clock)),
      logger_(std::move(logger)),
      is_room_available_(std::move(is_room_available)),
      counters_(counters) {
  message_.format = kJpegFormat;
}

InboundVideoTrack::~InboundVideoTrack() { stop(); }

bool InboundVideoTrack::start() {
  try {
    thread_ = std::thread(&InboundVideoTrack::readLoop, this);
  } catch (const std::exception& e) {
    RCLCPP_ERROR(logger_, "Failed to start reader thread for LiveKit video track '%s' from '%s': %s",
                 options_.track_name.c_str(), options_.publisher_identity.c_str(), e.what());
    return false;
  }
  return true;
}

void InboundVideoTrack::stop() {
  stop_requested_.store(true);
  if (stream_ && stream_->close) {
    stream_->close();
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void InboundVideoTrack::updateDemand() {
  if (!options_.set_enabled || !publisher_) {
    return;
  }
  const bool wanted = publisher_->get_subscription_count() > 0U;
  const std::lock_guard<std::mutex> lock(demand_mutex_);
  if (wanted != paused_.load(std::memory_order_relaxed)) {
    return;
  }
  if (!options_.set_enabled(wanted)) {
    RCLCPP_WARN(logger_, "Failed to %s LiveKit video track '%s' from '%s'", wanted ? "resume" : "pause",
                options_.track_name.c_str(), options_.publisher_identity.c_str());
    return;
  }
  paused_.store(!wanted, std::memory_order_relaxed);
  RCLCPP_INFO(logger_, "%s LiveKit video track '%s' from '%s'; '%s' has %s", wanted ? "Resumed" : "Paused",
              options_.track_name.c_str(), options_.publisher_identity.c_str(), options_.ros_topic_name.c_str(),
              wanted ? "a ROS subscriber" : "no ROS subscribers");
}

bool InboundVideoTrack::ownsPublisher(const rmw_gid_t& gid) const { return publisher_ && *publisher_ == gid; }

void InboundVideoTrack::readLoop() {
  const utils::ReaderAliveGuard reader_alive(reader_alive_);
  livekit::VideoFrameEvent event;
  while (!stop_requested_.load() && stream_ && stream_->read && stream_->read(event)) {
    if (is_room_available_ && !is_room_available_()) {
      continue;
    }
    // JPEG encoding is the dominant per-frame cost, so skip it while no ROS
    // subscriber (including rosbag) would receive the result.
    if (publisher_->get_subscription_count() == 0U) {
      continue;
    }
    publishFrame(event);
  }
}

void InboundVideoTrack::publishFrame(const livekit::VideoFrameEvent& event) {
  if (!encoder_.encodeI420(event.frame, message_.data)) {
    counters_.failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Dropping frame from LiveKit video track '%s' from '%s': %s",
                         options_.track_name.c_str(), options_.publisher_identity.c_str(),
                         encoder_.lastError().c_str());
    return;
  }

  fillHeader(event);

  try {
    publisher_->publish(message_);
  } catch (const std::exception& e) {
    counters_.failures.fetch_add(1, std::memory_order_relaxed);
    RCLCPP_WARN_THROTTLE(logger_, *clock_, 5000, "Failed to publish frame from LiveKit video track '%s' to '%s': %s",
                         options_.track_name.c_str(), options_.ros_topic_name.c_str(), e.what());
  }
}

void InboundVideoTrack::fillHeader(const livekit::VideoFrameEvent& event) {
  const auto& metadata = event.metadata;

  std::optional<builtin_interfaces::msg::Time> sender_stamp;
  if (metadata && metadata->user_timestamp_us) {
    sender_stamp = toRosTime(*metadata->user_timestamp_us);
  }
  if (sender_stamp) {
    message_.header.stamp = *sender_stamp;
  } else {
    message_.header.stamp = clock_->now();
    counters_.stamp_fallbacks.fetch_add(1, std::memory_order_relaxed);
    if (!stamp_fallback_logged_) {
      stamp_fallback_logged_ = true;
      RCLCPP_WARN(logger_,
                  "LiveKit video track '%s' from '%s' has no valid sender timestamp; stamping '%s' with the local "
                  "ROS clock",
                  options_.track_name.c_str(), options_.publisher_identity.c_str(), options_.ros_topic_name.c_str());
    }
  }

  std::optional<std::string_view> sender_frame_id;
  if (metadata && metadata->user_data) {
    sender_frame_id = utils::decodeFrameIdUserData(*metadata->user_data);
  }
  if (sender_frame_id) {
    // assign() reuses the string capacity, so a steady frame_id does not allocate.
    message_.header.frame_id.assign(sender_frame_id->data(), sender_frame_id->size());
  } else {
    message_.header.frame_id.assign(options_.fallback_frame_id);
  }
}

} // namespace ros_portal
