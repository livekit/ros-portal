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

#include <livekit/video_stream.h>
#include <rmw/types.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <rclcpp/clock.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/publisher.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <string>
#include <thread>

#include "ros_portal/types.hpp"
#include "ros_portal/utils/jpeg_encoder.hpp"

namespace ros_portal {

/// @brief Republish one remote LiveKit video track as a ROS
/// `sensor_msgs/CompressedImage` topic.
///
/// A background reader thread reads decoded I420 frames, encodes each one as
/// JPEG, and publishes it. The header stamp and frame_id come from the sender's
/// frame metadata when present. Otherwise the stamp comes from the local ROS
/// clock and the frame_id from @ref Options::fallback_frame_id.
///
/// Threading: construct, @ref start, @ref stop and destroy from one owner
/// thread. @ref ownsPublisher and @ref isReaderAlive are safe from any thread.
class InboundVideoTrack {
public:
  /// @brief Decoded frames from a remote LiveKit video track.
  struct Stream {
    /// @brief Block for the next frame. Returns false when the stream ends.
    std::function<bool(livekit::VideoFrameEvent&)> read;
    /// @brief Close the stream and unblock any pending read.
    std::function<void()> close;
  };

  /// @brief Identity and naming for one inbound video track.
  struct Options {
    /// @brief LiveKit track SID.
    std::string sid;
    /// @brief LiveKit track name.
    std::string track_name;
    /// @brief LiveKit participant identity of the remote publisher.
    std::string publisher_identity;
    /// @brief ROS topic that receives the compressed images.
    std::string ros_topic_name;
    /// @brief `header.frame_id` used when a frame carries no frame_id metadata.
    std::string fallback_frame_id;
  };

  /// @brief Cumulative counters owned by the caller and shared across tracks.
  struct Counters {
    /// @brief Frames that failed to encode or publish. Each one is logged.
    std::atomic<std::uint64_t> failures{0};
    /// @brief Frames stamped with the local ROS clock because the sender gave
    /// no `user_timestamp_us`.
    std::atomic<std::uint64_t> stamp_fallbacks{0};
  };

  /// @brief Create an inbound video track. Call @ref start to begin reading.
  /// @param options Track identity and naming.
  /// @param stream Open frame stream. Must have @ref Stream::read and
  /// @ref Stream::close set.
  /// @param publisher ROS publisher for @ref Options::ros_topic_name.
  /// @param clock Clock used for stamp fallback and throttled logging.
  /// @param logger Logger for frame failures.
  /// @param is_room_available Returns whether frames may be forwarded now.
  /// @param counters Counters that must outlive this object.
  InboundVideoTrack(Options options, std::shared_ptr<Stream> stream,
                    rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr publisher,
                    rclcpp::Clock::SharedPtr clock, rclcpp::Logger logger, IsRoomAvailableFn is_room_available,
                    Counters& counters);

  /// @brief Stop the reader thread.
  ~InboundVideoTrack();

  InboundVideoTrack(const InboundVideoTrack&) = delete;
  InboundVideoTrack& operator=(const InboundVideoTrack&) = delete;
  InboundVideoTrack(InboundVideoTrack&&) = delete;
  InboundVideoTrack& operator=(InboundVideoTrack&&) = delete;

  /// @brief Start the reader thread.
  /// @return False when the thread could not be created.
  [[nodiscard]] bool start();

  /// @brief Close the stream and join the reader thread. Safe to call twice.
  void stop();

  /// @brief Return whether @p gid identifies this track's ROS publisher.
  bool ownsPublisher(const rmw_gid_t& gid) const;

  /// @brief Return whether the reader thread is running.
  bool isReaderAlive() const { return reader_alive_.load(std::memory_order_relaxed); }

  /// @brief Return the track identity and naming.
  const Options& options() const { return options_; }

private:
  /// @brief Read frames until the stream ends or @ref stop is called.
  void readLoop();
  /// @brief Encode and publish one frame.
  void publishFrame(const livekit::VideoFrameEvent& event);
  /// @brief Fill the reused message header from the frame metadata.
  void fillHeader(const livekit::VideoFrameEvent& event);

  Options options_;
  std::shared_ptr<Stream> stream_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr publisher_;
  rclcpp::Clock::SharedPtr clock_;
  rclcpp::Logger logger_;
  IsRoomAvailableFn is_room_available_;
  Counters& counters_;
  /// @brief Encoder used only by the reader thread.
  utils::JpegEncoder encoder_;
  /// @brief Message reused for every frame so the hot path does not allocate.
  sensor_msgs::msg::CompressedImage message_;
  /// @brief True after the first stamp fallback is logged for this track.
  bool stamp_fallback_logged_{false};
  std::thread thread_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool reader_alive_{false};
};

} // namespace ros_portal
