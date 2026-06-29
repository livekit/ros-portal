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
#include <livekit/livekit.h>
#include <livekit/remote_participant.h>
#include <livekit/remote_track_publication.h>
#include <livekit/room.h>
#include <livekit/room_delegate.h>
#include <livekit/stats.h>
#include <livekit/track.h>
#include <livekit/video_stream.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "ros2_livekit_bridge/ros2_livekit_bridge.hpp"
#include "test_common.hpp"
namespace ros2_livekit_bridge::test {
namespace {

using namespace std::chrono_literals;

constexpr auto kGraphTimeout = 15s;
constexpr auto kStatsTimeout = 12s;
constexpr auto kStatsRequestTimeout = 5s;
constexpr auto kStableStatsWindow = 2s;
constexpr auto kStatsPollInterval = 250ms;
// Use a realistic camera resolution. The bridge publishes a single
// (non-simulcast) encoding, which the SDK registers against the higher
// dynacast quality classes. Very small frames (e.g. 160x120) make the SFU
// enable only the "Low" quality class, which leaves that lone encoding
// disabled and suspends the encoder, so no RTP is ever sent even with an
// active subscriber.
constexpr std::uint32_t kFrameWidth = 640;
constexpr std::uint32_t kFrameHeight = 480;

struct OutboundVideoCounters {
  std::uint64_t bytes_sent{0};
  std::uint64_t packets_sent{0};
  std::uint64_t frames_sent{0};

  bool operator==(const OutboundVideoCounters& other) const {
    return bytes_sent == other.bytes_sent && packets_sent == other.packets_sent && frames_sent == other.frames_sent;
  }
};

struct InboundVideoCounters {
  std::uint64_t bytes_received{0};
  std::uint64_t packets_received{0};
  std::uint64_t frames_received{0};
};

std::string dynacastTopicName() { return "/dynacast/camera_" + std::to_string(::getpid()); }

std::string dynacastConfigYaml(const std::string& topic_name) {
  std::ostringstream stream;
  stream << "ros2_livekit_bridge:\n"
         << "  version: \"0.0.1\"\n"
         << "  topic_polling_period_ms: 50\n"
         << "  ros_threads: 4\n"
         << "  topics:\n"
         << "    - topic: \"" << topic_name << "\"\n"
         << "      direction: \"out\"\n";
  return stream.str();
}

rclcpp::NodeOptions createBridgeOptions(const rclcpp::Context::SharedPtr& context, const std::string& node_namespace,
                                        const std::string& config_path) {
  return rclcpp::NodeOptions()
      .context(context)
      .arguments({"--ros-args", "-r", "__ns:=" + node_namespace})
      .parameter_overrides({
          rclcpp::Parameter("config_path", config_path),
      });
}

rclcpp::ExecutorOptions executorOptions(const rclcpp::Context::SharedPtr& context) {
  auto options = rclcpp::ExecutorOptions{};
  options.context = context;
  return options;
}

sensor_msgs::msg::Image makeImageFrame(rclcpp::Clock& clock, std::uint64_t frame_index) {
  sensor_msgs::msg::Image msg;
  msg.header.stamp = clock.now();
  msg.height = kFrameHeight;
  msg.width = kFrameWidth;
  msg.encoding = "rgba8";
  msg.is_bigendian = false;
  msg.step = kFrameWidth * 4;
  msg.data.resize(static_cast<std::size_t>(msg.step) * msg.height);

  const auto frame_byte = static_cast<std::uint8_t>(frame_index % 255U);
  for (std::size_t i = 0; i < msg.data.size(); i += 4) {
    msg.data[i + 0] = static_cast<std::uint8_t>(20U + frame_byte % 80U);
    msg.data[i + 1] = static_cast<std::uint8_t>(40U + frame_byte % 100U);
    msg.data[i + 2] = static_cast<std::uint8_t>(120U + frame_byte % 60U);
    msg.data[i + 3] = 255U;
  }

  return msg;
}

OutboundVideoCounters outboundVideoCounters(const livekit::SessionStats& stats) {
  OutboundVideoCounters counters;
  for (const auto& rtc_stats : stats.publisher_stats) {
    std::visit(
        [&counters](const auto& typed_stats) {
          using StatsType = std::decay_t<decltype(typed_stats)>;
          if constexpr (std::is_same_v<StatsType, livekit::RtcOutboundRtpStats>) {
            if (typed_stats.stream.kind == "video") {
              counters.bytes_sent += typed_stats.sent.bytes_sent;
              counters.packets_sent += typed_stats.sent.packets_sent;
              counters.frames_sent += typed_stats.outbound.frames_sent;
            }
          }
        },
        rtc_stats.stats);
  }
  return counters;
}

InboundVideoCounters inboundVideoCounters(const livekit::SessionStats& stats) {
  InboundVideoCounters counters;
  for (const auto& rtc_stats : stats.subscriber_stats) {
    std::visit(
        [&counters](const auto& typed_stats) {
          using StatsType = std::decay_t<decltype(typed_stats)>;
          if constexpr (std::is_same_v<StatsType, livekit::RtcInboundRtpStats>) {
            if (typed_stats.stream.kind == "video") {
              counters.bytes_received += typed_stats.inbound.bytes_received;
              counters.packets_received += typed_stats.received.packets_received;
              counters.frames_received += typed_stats.inbound.frames_received;
            }
          }
        },
        rtc_stats.stats);
  }
  return counters;
}

std::shared_ptr<livekit::RemoteTrackPublication> findRemoteVideoPublication(livekit::Room& room,
                                                                            const std::string& track_name) {
  for (const auto& weak_participant : room.remoteParticipants()) {
    auto participant = weak_participant.lock();
    if (!participant) {
      continue;
    }

    for (const auto& entry : participant->trackPublications()) {
      const auto& publication = entry.second;
      if (publication && publication->name() == track_name && publication->kind() == livekit::TrackKind::KIND_VIDEO) {
        return publication;
      }
    }
  }
  return nullptr;
}

std::shared_ptr<livekit::RemoteTrackPublication> waitForRemoteVideoPublication(livekit::Room& room,
                                                                               const std::string& track_name,
                                                                               std::chrono::milliseconds timeout) {
  std::shared_ptr<livekit::RemoteTrackPublication> publication;
  if (!waitFor(
          [&]() {
            publication = findRemoteVideoPublication(room, track_name);
            return publication != nullptr;
          },
          timeout)) {
    return nullptr;
  }
  return publication;
}

std::optional<livekit::SessionStats> waitForStatsFuture(std::future<livekit::SessionStats> future) {
  try {
    if (future.wait_for(kStatsRequestTimeout) != std::future_status::ready) {
      return std::nullopt;
    }
    return future.get();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<OutboundVideoCounters> sampleBridgeOutboundVideoCounters(Ros2LiveKitBridge& bridge) {
  auto result = bridge.getLiveKitSessionStats();
  if (!result) {
    return std::nullopt;
  }

  auto stats = waitForStatsFuture(std::move(result).value());
  if (!stats) {
    return std::nullopt;
  }
  return outboundVideoCounters(*stats);
}

std::optional<InboundVideoCounters> sampleObserverInboundVideoCounters(const std::shared_ptr<livekit::Track>& track) {
  if (!track) {
    return std::nullopt;
  }

  try {
    auto future = track->getStats();
    if (future.wait_for(kStatsRequestTimeout) != std::future_status::ready) {
      return std::nullopt;
    }
    livekit::SessionStats stats;
    stats.subscriber_stats = future.get();
    return inboundVideoCounters(stats);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

template <typename Sampler>
bool outboundVideoStopsGrowing(Sampler&& sampler, std::chrono::milliseconds timeout) {
  auto baseline = sampler();
  if (!baseline) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(kStableStatsWindow);
    auto next = sampler();
    if (!next) {
      continue;
    }
    if (*next == *baseline) {
      return true;
    }
    baseline = *next;
  }
  return false;
}

template <typename Sampler>
bool outboundVideoGrows(Sampler&& sampler, std::chrono::milliseconds timeout) {
  auto baseline = sampler();
  if (!baseline) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(kStatsPollInterval);
    auto next = sampler();
    if (!next) {
      continue;
    }
    if (next->bytes_sent > baseline->bytes_sent && next->packets_sent > baseline->packets_sent &&
        next->frames_sent > baseline->frames_sent) {
      return true;
    }
  }
  return false;
}

template <typename Sampler>
bool inboundVideoGrows(Sampler&& sampler, std::chrono::milliseconds timeout) {
  auto baseline = sampler();
  if (!baseline) {
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(kStatsPollInterval);
    auto next = sampler();
    if (!next) {
      continue;
    }
    if (next->bytes_received > baseline->bytes_received && next->packets_received > baseline->packets_received &&
        next->frames_received > baseline->frames_received) {
      return true;
    }
  }
  return false;
}

class DynacastObserverDelegate final : public livekit::RoomDelegate {
public:
  explicit DynacastObserverDelegate(std::string track_name) : track_name_(std::move(track_name)) {}

  void onTrackPublished(livekit::Room&, const livekit::TrackPublishedEvent& event) override {
    if (!isTargetVideoPublication(event.publication)) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      publication_ = event.publication;
    }
    cv_.notify_all();
  }

  void onTrackSubscribed(livekit::Room&, const livekit::TrackSubscribedEvent& event) override {
    if (!isTargetVideoPublication(event.publication)) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      subscribed_track_ = event.track;
    }
    cv_.notify_all();
  }

  void onTrackUnsubscribed(livekit::Room&, const livekit::TrackUnsubscribedEvent& event) override {
    if (!isTargetVideoPublication(event.publication)) {
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      subscribed_track_.reset();
      ++unsubscribed_count_;
    }
    cv_.notify_all();
  }

  void onTrackSubscriptionFailed(livekit::Room&, const livekit::TrackSubscriptionFailedEvent& event) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      subscription_error_ = event.error;
    }
    cv_.notify_all();
  }

  std::shared_ptr<livekit::Track> waitForSubscribedTrack(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [&]() { return subscribed_track_ != nullptr; })) {
      return nullptr;
    }
    return subscribed_track_;
  }

  bool waitForUnsubscribed(std::uint64_t previous_unsubscribed_count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [&]() { return unsubscribed_count_ > previous_unsubscribed_count; });
  }

  std::uint64_t unsubscribedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return unsubscribed_count_;
  }

  std::string subscriptionError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscription_error_;
  }

private:
  bool isTargetVideoPublication(const std::shared_ptr<livekit::RemoteTrackPublication>& publication) const {
    return publication && publication->name() == track_name_ && publication->kind() == livekit::TrackKind::KIND_VIDEO;
  }

  const std::string track_name_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::shared_ptr<livekit::RemoteTrackPublication> publication_;
  std::shared_ptr<livekit::Track> subscribed_track_;
  std::uint64_t unsubscribed_count_{0};
  std::string subscription_error_;
};

class ScopedVideoStreamReader {
public:
  explicit ScopedVideoStreamReader(const std::shared_ptr<livekit::Track>& track) {
    livekit::VideoStream::Options options;
    options.capacity = 2;
    stream_ = livekit::VideoStream::fromTrack(track, options);
    thread_ = std::thread([this]() {
      livekit::VideoFrameEvent event;
      while (!stop_.load() && stream_ && stream_->read(event)) {
        ++frames_read_;
      }
    });
  }

  ~ScopedVideoStreamReader() {
    stop_.store(true);
    if (stream_) {
      stream_->close();
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::uint64_t framesRead() const { return frames_read_.load(); }

private:
  std::shared_ptr<livekit::VideoStream> stream_;
  std::thread thread_;
  std::atomic_bool stop_{false};
  std::atomic<std::uint64_t> frames_read_{0};
};

class BridgeDynacastE2E : public ::testing::Test {
protected:
  static void SetUpTestSuite() { livekit::initialize(livekit::LogLevel::Info); }

  static void TearDownTestSuite() { livekit::shutdown(); }

  void SetUp() override {
    std::string source;
    livekit_url_ = utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    bridge_token_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_A", source);
    observer_token_ = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN_B", source);

    const auto original_token = utils::resolveEnvironmentCredential("LIVEKIT_TOKEN", source);
    original_token_ = original_token.empty() ? std::nullopt : std::optional<std::string>(original_token);

    const auto original_livekit_url = utils::resolveEnvironmentCredential("LIVEKIT_URL", source);
    original_livekit_url_ =
        original_livekit_url.empty() ? std::nullopt : std::optional<std::string>(original_livekit_url);
  }

  void TearDown() override {
    shutdownRuntime();
    restoreEnv("LIVEKIT_TOKEN", original_token_);
    restoreEnv("LIVEKIT_URL", original_livekit_url_);
  }

  bool configured() const { return !livekit_url_.empty() && !bridge_token_.empty() && !observer_token_.empty(); }

  void initializeRuntime(const std::string& topic_name, bool observer_auto_subscribe = false) {
    ASSERT_TRUE(configured()) << "LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B must be set";

    const auto [domain_id, _] = testDomainIds();
    graph_ = std::make_unique<ScopedRosGraph>(domain_id);
    config_file_ =
        std::make_unique<TemporaryConfigFile>(dynacastConfigYaml(topic_name), "ros2_livekit_bridge_dynacast_e2e_");

    ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", bridge_token_));
    ASSERT_TRUE(setEnv("LIVEKIT_URL", livekit_url_));

    bridge_ = std::make_shared<Ros2LiveKitBridge>(
        createBridgeOptions(graph_->context(), "/bridge_dynacast_node", config_file_->path().string()));
    ASSERT_TRUE(bridge_->initialize());

    image_node_ =
        std::make_shared<rclcpp::Node>("dynacast_image_publisher", rclcpp::NodeOptions().context(graph_->context()));

    executor_ = std::make_unique<rclcpp::executors::MultiThreadedExecutor>(executorOptions(graph_->context()), 2);
    executor_->add_node(bridge_);
    executor_->add_node(image_node_);
    spinning_.store(true);
    spin_thread_ = std::thread([this]() {
      executor_->spin();
      spinning_.store(false);
    });

    observer_delegate_ = std::make_unique<DynacastObserverDelegate>(topic_name);
    observer_room_ = std::make_unique<livekit::Room>();
    observer_room_->setDelegate(observer_delegate_.get());

    livekit::RoomOptions room_options;
    room_options.auto_subscribe = observer_auto_subscribe;
    room_options.dynacast = false;
    // Disable adaptive stream so the observer receives video without having to
    // report a render viewport; the dynacast assertions depend solely on
    // subscription state driving the publisher.
    room_options.adaptive_stream = false;
    ASSERT_TRUE(observer_room_->connect(livekit_url_, observer_token_, room_options));

    publisher_ = image_node_->create_publisher<sensor_msgs::msg::Image>(topic_name, 10);
    publish_timer_ = image_node_->create_wall_timer(
        33ms, [this]() { publisher_->publish(makeImageFrame(*image_node_->get_clock(), frame_index_++)); });

    ASSERT_TRUE(waitFor([&]() { return publisher_->get_subscription_count() > 0; }, kGraphTimeout))
        << "Bridge did not subscribe to " << topic_name;
  }

  void shutdownRuntime() {
    publish_timer_.reset();
    publisher_.reset();

    if (observer_room_) {
      observer_room_.reset();
    }
    observer_delegate_.reset();

    if (executor_ && spinning_.exchange(false)) {
      executor_->cancel();
    }
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
    if (executor_) {
      if (image_node_) {
        executor_->remove_node(image_node_);
      }
      if (bridge_) {
        executor_->remove_node(bridge_);
      }
    }

    image_node_.reset();
    bridge_.reset();
    executor_.reset();
    graph_.reset();
    config_file_.reset();
  }

  Ros2LiveKitBridge& bridge() { return *bridge_; }
  livekit::Room& observerRoom() { return *observer_room_; }
  DynacastObserverDelegate& observerDelegate() { return *observer_delegate_; }

private:
  std::string livekit_url_;
  std::string bridge_token_;
  std::string observer_token_;
  std::optional<std::string> original_token_;
  std::optional<std::string> original_livekit_url_;

  std::unique_ptr<ScopedRosGraph> graph_;
  std::unique_ptr<TemporaryConfigFile> config_file_;
  std::shared_ptr<Ros2LiveKitBridge> bridge_;
  std::shared_ptr<rclcpp::Node> image_node_;
  std::shared_ptr<rclcpp::Publisher<sensor_msgs::msg::Image>> publisher_;
  rclcpp::TimerBase::SharedPtr publish_timer_;
  std::uint64_t frame_index_{0};

  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::thread spin_thread_;
  std::atomic_bool spinning_{false};

  std::unique_ptr<livekit::Room> observer_room_;
  std::unique_ptr<DynacastObserverDelegate> observer_delegate_;
};

// Positive control for the dynacast pause/resume assertions below.
//
// With an auto-subscribing observer there is an active subscriber from the
// moment the track is published, so dynacast should keep the track active and
// media should flow end-to-end (publisher encodes, SFU forwards, observer
// decodes). If this baseline passes but PausesOutboundVideoWhenNoActive
// Subscribers fails, the media path is healthy and the failure is specific to
// the dynacast resume transition. If this baseline also fails, the harness or
// server media path is broken rather than dynacast itself.
TEST_F(BridgeDynacastE2E, StreamsOutboundVideoToAutoSubscribedObserver) {
  const auto topic_name = dynacastTopicName();
  initializeRuntime(topic_name, /*observer_auto_subscribe=*/true);

  auto subscribed_track = observerDelegate().waitForSubscribedTrack(kGraphTimeout);
  ASSERT_NE(subscribed_track, nullptr) << "Observer did not auto-subscribe to " << topic_name << ": "
                                       << observerDelegate().subscriptionError();

  ScopedVideoStreamReader video_reader(subscribed_track);

  auto outbound_sampler = [&]() { return sampleBridgeOutboundVideoCounters(bridge()); };
  EXPECT_TRUE(outboundVideoGrows(outbound_sampler, kStatsTimeout))
      << "Bridge outbound video RTP did not flow to an auto-subscribed observer";

  auto inbound_sampler = [&]() { return sampleObserverInboundVideoCounters(subscribed_track); };
  EXPECT_TRUE(inboundVideoGrows(inbound_sampler, kStatsTimeout))
      << "Observer inbound video RTP did not increase while auto-subscribed";

  EXPECT_TRUE(waitFor([&]() { return video_reader.framesRead() > 0; }, kStatsTimeout))
      << "Observer did not decode any video frames while auto-subscribed";
}

TEST_F(BridgeDynacastE2E, PausesOutboundVideoWhenNoActiveSubscribers) {
  const auto topic_name = dynacastTopicName();
  initializeRuntime(topic_name);

  auto publication = waitForRemoteVideoPublication(observerRoom(), topic_name, kGraphTimeout);
  ASSERT_NE(publication, nullptr) << "Observer did not see LiveKit video publication for " << topic_name;

  publication->setSubscribed(false);
  auto outbound_sampler = [&]() { return sampleBridgeOutboundVideoCounters(bridge()); };
  EXPECT_TRUE(outboundVideoStopsGrowing(outbound_sampler, kStatsTimeout))
      << "Bridge outbound video RTP kept growing before subscription";

  publication->setSubscribed(true);
  auto subscribed_track = observerDelegate().waitForSubscribedTrack(kGraphTimeout);
  ASSERT_NE(subscribed_track, nullptr) << "Observer did not subscribe to " << topic_name << ": "
                                       << observerDelegate().subscriptionError();

  {
    ScopedVideoStreamReader video_reader(subscribed_track);
    EXPECT_TRUE(outboundVideoGrows(outbound_sampler, kStatsTimeout))
        << "Bridge outbound video RTP did not resume with an active subscriber";

    auto inbound_sampler = [&]() { return sampleObserverInboundVideoCounters(subscribed_track); };
    EXPECT_TRUE(inboundVideoGrows(inbound_sampler, kStatsTimeout))
        << "Observer inbound video RTP did not increase while subscribed";

    EXPECT_TRUE(waitFor([&]() { return video_reader.framesRead() > 0; }, kStatsTimeout))
        << "Observer did not decode any video frames while subscribed";
  }

  const auto previous_unsubscribed_count = observerDelegate().unsubscribedCount();
  publication->setSubscribed(false);
  EXPECT_TRUE(observerDelegate().waitForUnsubscribed(previous_unsubscribed_count, kGraphTimeout))
      << "Observer did not receive a video unsubscribe event";

  EXPECT_TRUE(outboundVideoStopsGrowing(outbound_sampler, kStatsTimeout))
      << "Bridge outbound video RTP kept growing after unsubscribe";
}

} // namespace
} // namespace ros2_livekit_bridge::test
