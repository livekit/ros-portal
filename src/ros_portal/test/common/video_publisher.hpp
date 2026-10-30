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

#include <livekit/local_participant.h>
#include <livekit/local_video_track.h>
#include <livekit/room.h>
#include <livekit/video_frame.h>
#include <livekit/video_source.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "ros_portal/utils/video_frame_metadata.hpp"

namespace ros_portal::test {
using namespace std::chrono_literals;

/// @brief Publish synthetic video with metadata from an independent LiveKit participant.
class VideoPublisher {
public:
  /// @brief Connect and publish a track named @p track_name.
  /// @param url LiveKit server URL.
  /// @param token Participant access token.
  /// @param track_name Track name used to select the portal ROS topic.
  /// @return True when the room and local participant are available.
  bool connect(const std::string& url, const std::string& token,
               const std::string& track_name = "/e2e_camera/image_raw") {
    livekit::RoomOptions room_options;
    room_options.auto_subscribe = false;
    if (!room_.connect(url, token, room_options)) {
      return false;
    }
    const auto participant = room_.localParticipant().lock();
    if (!participant) {
      return false;
    }

    source_ = std::make_shared<livekit::VideoSource>(64, 48);
    track_ = livekit::LocalVideoTrack::createLocalVideoTrack(track_name, source_);
    livekit::TrackPublishOptions publish_options;
    publish_options.source = livekit::TrackSource::SOURCE_CAMERA;
    publish_options.frame_metadata_features = livekit::FrameMetadataFeatures{};
    publish_options.frame_metadata_features->user_timestamp = true;
    publish_options.frame_metadata_features->user_data = true;
    participant->publishTrack(track_, publish_options);
    return true;
  }

  /// @brief Push frames until destruction to keep late subscribers receiving video.
  void start() {
    capture_thread_ = std::thread([this]() {
      auto frame = livekit::VideoFrame::create(64, 48, livekit::VideoBufferType::RGBA);
      std::memset(frame.data(), 128, frame.dataSize());
      livekit::VideoCaptureOptions capture_options;
      capture_options.metadata.emplace();
      capture_options.metadata->user_timestamp_us = 1'700'000'123'456U;
      capture_options.metadata->user_data = utils::encodeFrameIdUserData("e2e_camera_optical_frame");
      while (!stop_.load()) {
        capture_options.timestamp_us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        source_->captureFrame(frame, capture_options);
        std::this_thread::sleep_for(33ms);
      }
    });
  }

  /// @brief Capture one correlated synthetic frame after the stream is warm.
  /// @param frame_id ROS frame identity carried in metadata.
  /// @param timestamp_us Sender timestamp in microseconds.
  /// @note Call from one thread, without start() running.
  void capture(const std::string& frame_id, std::uint64_t timestamp_us) {
    // Acknowledgment can be faster than the encoder's frame-rate limit.
    // Pace on the test thread, never in an executor callback (at most 100 ms).
    std::this_thread::sleep_until(next_capture_);
    next_capture_ = std::chrono::steady_clock::now() + 100ms;
    auto frame = livekit::VideoFrame::create(64, 48, livekit::VideoBufferType::RGBA);
    std::memset(frame.data(), static_cast<int>(64U + timestamp_us % 128U), frame.dataSize());
    livekit::VideoCaptureOptions options;
    options.timestamp_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    options.metadata.emplace();
    options.metadata->user_timestamp_us = timestamp_us;
    options.metadata->user_data = utils::encodeFrameIdUserData(frame_id);
    source_->captureFrame(frame, options);
  }

  ~VideoPublisher() {
    stop_.store(true);
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
  }

private:
  livekit::Room room_;
  std::shared_ptr<livekit::VideoSource> source_;
  std::shared_ptr<livekit::LocalVideoTrack> track_;
  std::thread capture_thread_;
  std::atomic_bool stop_{false};
  std::chrono::steady_clock::time_point next_capture_{};
};

} // namespace ros_portal::test
