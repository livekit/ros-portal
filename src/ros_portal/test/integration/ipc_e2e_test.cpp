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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sstream>
#include <string>

#include "ros_portal_e2e_fixture.hpp"
#include "testing/message_observer.hpp"
#include "video_publisher.hpp"

namespace ros_portal::test {
namespace {

constexpr char kIpcTopic[] = "/ipc_camera/image_raw";
constexpr char kFrameId[] = "ipc_camera_frame";
constexpr std::uint64_t kTimestampBaseUs = 1'700'000'000'000U;
constexpr std::uint64_t kFrameCount = 3U;

/// @brief Capture addresses and contents without taking ownership of a message.
struct AddressObservation {
  std::atomic<std::uintptr_t> message{0};
  std::atomic<std::uintptr_t> payload{0};
  std::size_t bytes{0};
  std::uint64_t checksum{0};

  template <typename MessageT>
  void record(const MessageT& value) noexcept {
    message.store(reinterpret_cast<std::uintptr_t>(&value), std::memory_order_relaxed);
    payload.store(reinterpret_cast<std::uintptr_t>(value.data.data()), std::memory_order_relaxed);
    bytes = value.data.size();
    checksum = 0;
    for (const auto byte : value.data) {
      checksum = checksum * 31U + byte;
    }
  }
};

/// @brief Synchronize one outstanding frame using release/acquire acknowledgments.
/// Non-atomic observations are read only after the matching ID is acquired;
/// another frame cannot overwrite them until the test acknowledges this one.
class BoundaryObserver final : public MessageObserver {
public:
  void imageReceived(const sensor_msgs::msg::Image& message, const rclcpp::MessageInfo& info) noexcept override {
    if (message.header.frame_id != kFrameId) {
      return;
    }
    received.record(message);
    intra_process = info.get_rmw_message_info().from_intra_process;
    contents_valid = message.width == 64U && message.height == 48U && message.step == 192U &&
                     message.encoding == "rgb8" && message.data.size() == std::size_t{64} * 48U * 3U &&
                     std::all_of(message.data.begin(), message.data.end(), [](auto byte) { return byte == 128U; });
    received_id.store(message.header.stamp.nanosec / 1000U, std::memory_order_release);
  }

  void imagePublishing(const sensor_msgs::msg::CompressedImage& message) noexcept override {
    if (message.header.frame_id != kFrameId) {
      return;
    }
    sent.record(message);
    sent_id.store(message.header.stamp.nanosec / 1000U, std::memory_order_release);
  }

  AddressObservation sent;
  AddressObservation received;
  std::atomic<std::uint64_t> sent_id{0};
  std::atomic<std::uint64_t> received_id{0};
  bool intra_process{false};
  bool contents_valid{false};
};

class PortalIpcE2E : public RosPortalTestE2E {
protected:
  void initializeIpc(const std::string& direction) {
    ASSERT_TRUE(configured()) << "Source scripts/set-test-tokens.sh; a LiveKit server with frame metadata is required";
    ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", tokenB()));
    graph_ = std::make_unique<ScopedRosGraph>(testDomainIds().first);
    config_ = std::make_unique<TemporaryConfigFile>(
        rosPortalConfigYaml(kIpcTopic, false, direction) + "  video_sources: []\n", "ros_portal_ipc_");
    message_observer.store(&observations_, std::memory_order_release);
    portal_ =
        std::make_shared<RosPortal>(createRosPortalOptions(graph_->context(), "/ipc_portal", config_->path().string())
                                        .use_intra_process_comms(true));
    app_ = std::make_shared<rclcpp::Node>(
        "ipc_test_app", rclcpp::NodeOptions().context(graph_->context()).use_intra_process_comms(true));
    rclcpp::ExecutorOptions options;
    options.context = graph_->context();
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(options);
    executor_->add_node(portal_);
    executor_->add_node(app_);
  }

  template <typename Predicate>
  bool spinUntil(Predicate&& predicate) {
    return waitFor(
        [&]() {
          executor_->spin_some();
          return predicate();
        },
        kMessageTimeout, 10ms);
  }

  void TearDown() override {
    executor_.reset();
    // Join portal reader threads before removing their non-owning observer.
    portal_.reset();
    app_.reset();
    graph_.reset();
    config_.reset();
    message_observer.store(nullptr, std::memory_order_release);
    RosPortalTestE2E::TearDown();
  }

  BoundaryObserver observations_;
  std::unique_ptr<ScopedRosGraph> graph_;
  std::unique_ptr<TemporaryConfigFile> config_;
  std::shared_ptr<RosPortal> portal_;
  std::shared_ptr<rclcpp::Node> app_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

TEST_F(PortalIpcE2E, AppToPortalPreservesMessageAndPayloadWithConstSharedPtr) {
  ASSERT_NO_FATAL_FAILURE(initializeIpc("out"));
  auto publisher = app_->create_publisher<sensor_msgs::msg::Image>(kIpcTopic, rclcpp::QoS(10));
  livekit::Room peer;
  ASSERT_TRUE(peer.connect(liveKitUrl(), tokenA(), livekit::RoomOptions{}));
  ASSERT_TRUE(spinUntil([&]() { return portal_->hasParticipant(identityA()); }))
      << "Portal did not connect to the LiveKit room";
  ASSERT_TRUE(spinUntil([&]() { return publisher->get_intra_process_subscription_count() == 1U; }))
      << "Portal's typed IPC subscription did not match";

  for (std::uint64_t id = 1; id <= kFrameCount; ++id) {
    SCOPED_TRACE("app -> portal frame " + std::to_string(id));
    auto message = std::make_unique<sensor_msgs::msg::Image>();
    message->header.frame_id = kFrameId;
    message->header.stamp.sec = 1'700'000;
    message->header.stamp.nanosec = static_cast<std::uint32_t>(id * 1000U);
    message->width = 64;
    message->height = 48;
    message->step = 192;
    message->encoding = "rgb8";
    message->data.assign(std::size_t{64} * 48U * 3U, 128U);
    observations_.sent.record(*message);
    observations_.sent_id.store(id, std::memory_order_release);
    publisher->publish(std::move(message));

    ASSERT_TRUE(spinUntil([&]() { return observations_.received_id.load(std::memory_order_acquire) == id; }))
        << "No callback acknowledgment for frame " << id
        << "; sent message=" << observations_.sent.message.load(std::memory_order_relaxed)
        << ", received=" << observations_.received.message.load(std::memory_order_relaxed);
    EXPECT_TRUE(observations_.intra_process);
    EXPECT_TRUE(observations_.contents_valid);
    EXPECT_EQ(observations_.received.message.load(std::memory_order_relaxed),
              observations_.sent.message.load(std::memory_order_relaxed));
    EXPECT_EQ(observations_.received.payload.load(std::memory_order_relaxed),
              observations_.sent.payload.load(std::memory_order_relaxed));
    EXPECT_EQ(observations_.received.checksum, observations_.sent.checksum);
  }
}

TEST_F(PortalIpcE2E, PortalToAppUsesIpcWithConstSharedPtrAndReportsPublicationCopy) {
  ASSERT_NO_FATAL_FAILURE(initializeIpc("in"));
  bool warmed = false;
  auto subscription = app_->create_subscription<sensor_msgs::msg::CompressedImage>(
      kIpcTopic, rclcpp::QoS(10),
      [&](const sensor_msgs::msg::CompressedImage::ConstSharedPtr& message, const rclcpp::MessageInfo& info) {
        if (message->header.frame_id != kFrameId) {
          warmed = message->header.frame_id == "ipc_warmup" && info.get_rmw_message_info().from_intra_process;
          return;
        }
        observations_.received.record(*message);
        observations_.intra_process = info.get_rmw_message_info().from_intra_process;
        observations_.contents_valid = message->format == "jpeg" && message->header.stamp.sec == 1'700'000 &&
                                       message->data.size() > 2U && message->data[0] == 0xFFU &&
                                       message->data[1] == 0xD8U;
        observations_.received_id.store(message->header.stamp.nanosec / 1000U, std::memory_order_release);
      });
  VideoPublisher publisher;
  ASSERT_TRUE(publisher.connect(liveKitUrl(), tokenA(), kIpcTopic));
  ASSERT_TRUE(spinUntil([&]() { return portal_->hasParticipant(identityA()); }));
  ASSERT_TRUE(spinUntil([&]() { return subscription->get_publisher_count() == 1U; }));

  // Warm the real encoder/decoder and key-frame negotiation before sending
  // correlated frames. Warm-up frames never enter the address record.
  ASSERT_TRUE(waitFor(
      [&]() {
        publisher.capture("ipc_warmup", kTimestampBaseUs);
        executor_->spin_some();
        return warmed;
      },
      kMessageTimeout, 50ms))
      << "Remote video stream never warmed up";

  for (std::uint64_t id = 1; id <= kFrameCount; ++id) {
    SCOPED_TRACE("portal -> app frame " + std::to_string(id));
    publisher.capture(kFrameId, kTimestampBaseUs + id);
    ASSERT_TRUE(spinUntil([&]() { return observations_.received_id.load(std::memory_order_acquire) == id; }))
        << "No callback acknowledgment for frame " << id
        << "; published frame=" << observations_.sent_id.load(std::memory_order_acquire)
        << ", received frame=" << observations_.received_id.load(std::memory_order_acquire);
    ASSERT_EQ(observations_.sent_id.load(std::memory_order_acquire), id);
    EXPECT_TRUE(observations_.intra_process);
    EXPECT_TRUE(observations_.contents_valid);
    EXPECT_EQ(observations_.received.bytes, observations_.sent.bytes);
    EXPECT_EQ(observations_.received.checksum, observations_.sent.checksum);
    // publish(const Message&) copies into ROS-owned storage even with IPC.
    EXPECT_NE(observations_.received.message.load(std::memory_order_relaxed),
              observations_.sent.message.load(std::memory_order_relaxed));
    EXPECT_NE(observations_.received.payload.load(std::memory_order_relaxed),
              observations_.sent.payload.load(std::memory_order_relaxed));
  }
}

/// @brief Compare ROS delivery only, excluding frame preparation and video encoding.
class ImageIpcLatency : public ::testing::Test {
protected:
  using Clock = std::chrono::steady_clock;
  static constexpr std::uint32_t kWidth = 7680U;
  static constexpr std::uint32_t kHeight = 4320U;
  static constexpr std::uint32_t kSamples = 30U;
  static constexpr std::uint32_t kWarmupFrames = 3U;
  static constexpr std::size_t kPayloadBytes = std::size_t{kWidth} * kHeight * 3U;

  // Both callbacks run on the test thread's single-threaded executor. No
  // atomic synchronization or polling sleep is needed in the timed interval.
  struct Delivery {
    Clock::time_point sent_at;
    Clock::time_point received_at;
    std::int32_t expected_id{0};
    std::int32_t received_id{0};
    bool intra_process{false};
    bool contents_valid{false};
  };

  void measure(bool use_ipc, double& average_ms) {
    SCOPED_TRACE(use_ipc ? "IPC enabled" : "IPC disabled");
    const ScopedRosGraph graph(testDomainIds().first);
    const auto options = rclcpp::NodeOptions().context(graph.context()).use_intra_process_comms(use_ipc);
    auto sender = std::make_shared<rclcpp::Node>("latency_sender", options);
    auto receiver = std::make_shared<rclcpp::Node>("latency_receiver", options);
    const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
    auto publisher = sender->create_publisher<sensor_msgs::msg::Image>("/ipc_latency/image_raw", qos);
    Delivery delivery{};
    auto subscription = receiver->create_subscription<sensor_msgs::msg::Image>(
        "/ipc_latency/image_raw", qos,
        [&](const sensor_msgs::msg::Image::ConstSharedPtr& message, const rclcpp::MessageInfo& info) {
          const auto received_at = Clock::now();
          if (message->header.stamp.sec != delivery.expected_id) {
            return;
          }
          delivery.received_at = received_at;
          delivery.received_id = message->header.stamp.sec;
          delivery.intra_process = info.get_rmw_message_info().from_intra_process;
          const auto marker = static_cast<std::uint8_t>(delivery.expected_id);
          delivery.contents_valid = message->width == kWidth && message->height == kHeight &&
                                    message->encoding == "rgb8" && message->data.size() == kPayloadBytes &&
                                    message->data.front() == marker && message->data.back() == marker;
        });
    rclcpp::ExecutorOptions executor_options;
    executor_options.context = graph.context();
    rclcpp::executors::SingleThreadedExecutor executor(executor_options);
    executor.add_node(sender);
    executor.add_node(receiver);
    ASSERT_TRUE(waitFor(
        [&]() {
          return publisher->get_subscription_count() == 1U && subscription->get_publisher_count() == 1U &&
                 publisher->get_intra_process_subscription_count() == (use_ipc ? 1U : 0U);
        },
        kGraphTimeout))
        << "Image endpoints did not match";
    ASSERT_NO_FATAL_FAILURE(transmitFrames(*publisher, executor, delivery, use_ipc, average_ms));
  }

  void transmitFrames(rclcpp::Publisher<sensor_msgs::msg::Image>& publisher,
                      rclcpp::executors::SingleThreadedExecutor& executor, Delivery& delivery, bool use_ipc,
                      double& average_ms) {
    const auto deadline = Clock::now() + 60s;
    double total_ms = 0.0;
    for (std::uint32_t frame = 1U; frame <= kWarmupFrames + kSamples; ++frame) {
      SCOPED_TRACE("frame " + std::to_string(frame));
      ASSERT_LT(Clock::now(), deadline) << "8K delivery trial exceeded 60 seconds";
      // Allocate and touch the complete payload before starting the clock.
      // One outstanding image bounds memory; both modes publish by ownership transfer.
      auto message = std::make_unique<sensor_msgs::msg::Image>();
      message->width = kWidth;
      message->height = kHeight;
      message->step = kWidth * 3U;
      message->encoding = "rgb8";
      message->header.stamp.sec = static_cast<std::int32_t>(frame);
      message->data.assign(kPayloadBytes, static_cast<std::uint8_t>(frame));
      delivery.expected_id = message->header.stamp.sec;
      delivery.received_id = 0;
      delivery.sent_at = Clock::now();
      publisher.publish(std::move(message));
      while (delivery.received_id != delivery.expected_id && Clock::now() < deadline) {
        executor.spin_once(100ms);
      }
      ASSERT_EQ(delivery.received_id, delivery.expected_id) << "Timed out receiving an 8K image";
      ASSERT_EQ(delivery.intra_process, use_ipc);
      ASSERT_TRUE(delivery.contents_valid);
      if (frame > kWarmupFrames) {
        total_ms += std::chrono::duration<double, std::milli>(delivery.received_at - delivery.sent_at).count();
      }
    }
    average_ms = total_ms / static_cast<double>(kSamples);
  }
};

TEST_F(ImageIpcLatency, Thirty8kFramesArriveFasterWithIpc) {
  double ipc_on_ms = 0.0;
  double ipc_off_ms = 0.0;
  ASSERT_NO_FATAL_FAILURE(measure(true, ipc_on_ms));
  ASSERT_NO_FATAL_FAILURE(measure(false, ipc_off_ms));

  std::ostringstream report;
  report << std::fixed << std::setprecision(3) << "[  LATENCY ] 30 x 7680x4320 rgb8 (" << kPayloadBytes
         << " bytes/frame): IPC on = " << ipc_on_ms << " ms average, IPC off = " << ipc_off_ms << " ms average\n";
  std::cout << report.str() << std::flush;
  RecordProperty("ipc_on_average_ms", std::to_string(ipc_on_ms));
  RecordProperty("ipc_off_average_ms", std::to_string(ipc_off_ms));
  RecordProperty("measured_frames_per_mode", static_cast<int>(kSamples));
  EXPECT_LT(ipc_on_ms, ipc_off_ms) << report.str();
}

} // namespace
} // namespace ros_portal::test
