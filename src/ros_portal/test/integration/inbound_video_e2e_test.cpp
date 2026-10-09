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

#include <gtest/gtest.h>
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
#include <mutex>
#include <optional>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <string>
#include <thread>

#include "ros_portal/utils/video_frame_metadata.hpp"
#include "ros_portal_e2e_fixture.hpp"

namespace ros_portal::test {
namespace {

using namespace std::chrono_literals;

constexpr char kVideoTrackName[] = "/e2e_camera/image_raw";
constexpr char kCompressedTopic[] = "/e2e_camera/image_raw/compressed";
constexpr char kSenderFrameId[] = "e2e_camera_optical_frame";
constexpr std::uint64_t kSenderTimestampUs = 1'700'000'123'456U;
constexpr int kWidth = 64;
constexpr int kHeight = 48;

// Publishes a video track with frame metadata from an independent LiveKit
// participant, the same way a ROS Portal edge publishes a sensor_msgs/Image.
class VideoPublisher {
public:
  bool connect(const std::string& url, const std::string& token) {
    livekit::RoomOptions room_options;
    room_options.auto_subscribe = false;
    if (!room_.connect(url, token, room_options)) {
      return false;
    }
    const auto participant = room_.localParticipant().lock();
    if (!participant) {
      return false;
    }

    source_ = std::make_shared<livekit::VideoSource>(kWidth, kHeight);
    track_ = livekit::LocalVideoTrack::createLocalVideoTrack(kVideoTrackName, source_);
    livekit::TrackPublishOptions publish_options;
    publish_options.source = livekit::TrackSource::SOURCE_CAMERA;
    publish_options.frame_metadata_features = livekit::FrameMetadataFeatures{};
    publish_options.frame_metadata_features->user_timestamp = true;
    publish_options.frame_metadata_features->user_data = true;
    participant->publishTrack(track_, publish_options);
    return true;
  }

  // Push frames until stopped. The first decoded frames can arrive late, so the
  // test keeps the track live instead of sending a fixed number of frames.
  void start() {
    capture_thread_ = std::thread([this]() {
      auto frame = livekit::VideoFrame::create(kWidth, kHeight, livekit::VideoBufferType::RGBA);
      std::memset(frame.data(), 128, frame.dataSize());
      livekit::VideoCaptureOptions capture_options;
      capture_options.metadata.emplace();
      capture_options.metadata->user_timestamp_us = kSenderTimestampUs;
      capture_options.metadata->user_data = utils::encodeFrameIdUserData(kSenderFrameId);
      while (!stop_.load()) {
        capture_options.timestamp_us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        source_->captureFrame(frame, capture_options);
        std::this_thread::sleep_for(33ms);
      }
    });
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
};

TEST_F(RosPortalTestE2E, RepublishesRemoteVideoTrackAsCompressedImageWithSenderHeader) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  initializeInboundOnlyRuntime(kVideoTrackName);

  std::mutex mutex;
  std::optional<sensor_msgs::msg::CompressedImage> received;
  auto subscription = robotBNode()->create_subscription<sensor_msgs::msg::CompressedImage>(
      kCompressedTopic, 10, [&](const sensor_msgs::msg::CompressedImage::ConstSharedPtr& msg) {
        const std::lock_guard<std::mutex> lock(mutex);
        received = *msg;
      });

  VideoPublisher publisher;
  ASSERT_TRUE(publisher.connect(liveKitUrl(), tokenA())) << "Independent LiveKit video publisher failed to connect";
  publisher.start();

  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kCompressedTopic) > 0U; }, kGraphTimeout))
      << "ROS Portal did not create a publisher on " << kCompressedTopic;
  ASSERT_TRUE(waitFor(
      [&]() {
        const std::lock_guard<std::mutex> lock(mutex);
        return received.has_value();
      },
      kMessageTimeout))
      << "No CompressedImage arrived on " << kCompressedTopic;

  const std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ(received->format, "jpeg");
  ASSERT_GE(received->data.size(), 2U);
  EXPECT_EQ(received->data[0], 0xFF);
  EXPECT_EQ(received->data[1], 0xD8);
  EXPECT_EQ(received->header.frame_id, kSenderFrameId);
  EXPECT_EQ(received->header.stamp.sec, 1'700'000);
  EXPECT_EQ(received->header.stamp.nanosec, 123'456'000U);
}

// The portal pauses a track whose ROS topic has no subscribers. A subscriber
// that appears later must still receive frames, which needs the sender to
// produce a fresh key frame after the track resumes.
TEST_F(RosPortalTestE2E, ResumesPausedVideoTrackWhenRosSubscriberAppears) {
  ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";
  initializeInboundOnlyRuntime(kVideoTrackName);

  VideoPublisher publisher;
  ASSERT_TRUE(publisher.connect(liveKitUrl(), tokenA())) << "Independent LiveKit video publisher failed to connect";
  publisher.start();
  ASSERT_TRUE(waitFor([&]() { return robotBNode()->count_publishers(kCompressedTopic) > 0U; }, kGraphTimeout))
      << "ROS Portal did not create a publisher on " << kCompressedTopic;
  // Let several demand checks pass so the track is paused before subscribing.
  std::this_thread::sleep_for(2s);

  std::mutex mutex;
  std::optional<sensor_msgs::msg::CompressedImage> received;
  auto subscription = robotBNode()->create_subscription<sensor_msgs::msg::CompressedImage>(
      kCompressedTopic, 10, [&](const sensor_msgs::msg::CompressedImage::ConstSharedPtr& msg) {
        const std::lock_guard<std::mutex> lock(mutex);
        received = *msg;
      });

  ASSERT_TRUE(waitFor(
      [&]() {
        const std::lock_guard<std::mutex> lock(mutex);
        return received.has_value();
      },
      kMessageTimeout))
      << "No CompressedImage arrived on " << kCompressedTopic << " after the subscriber appeared";
  const std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ(received->header.frame_id, kSenderFrameId);
}

} // namespace
} // namespace ros_portal::test
