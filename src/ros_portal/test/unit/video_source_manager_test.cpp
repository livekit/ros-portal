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

#include "ros_portal/video_source_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ros_portal/capture_source_factory.hpp"

namespace ros_portal {
namespace {

std::optional<std::string> valueFor(const diagnostic_updater::DiagnosticStatusWrapper& status, const std::string& key) {
  for (const auto& value : status.values) {
    if (value.key == key) {
      return value.value;
    }
  }
  return std::nullopt;
}

ros_portal_config::VideoSourceConfig makeSource(std::string track_name, ros_portal_config::CaptureSourceType type) {
  ros_portal_config::VideoSourceConfig config;
  config.track_name = std::move(track_name);
  config.source.type = type;
  if (type == ros_portal_config::CaptureSourceType::Gstreamer) {
    config.source.pipeline = "videotestsrc ! x264enc ! appsink name=lk_appsink";
  }
  return config;
}

TEST(VideoSourceManagerTest, IsolatesStartupFailureAndReportsTerminalState) {
  std::map<std::string, diagnostics::DiagnosticsManagerFns::TaskCallback> diagnostic_callbacks;
  diagnostics::DiagnosticsManagerFns diagnostics;
  diagnostics.add = [&diagnostic_callbacks](const std::string& name, auto callback) {
    diagnostic_callbacks[name] = std::move(callback);
  };
  diagnostics.remove = [&diagnostic_callbacks](const std::string& name) { diagnostic_callbacks.erase(name); };

  int create_calls = 0;
  int start_calls = 0;
  int stop_calls = 0;
  int unpublish_calls = 0;
  std::optional<VideoSourceResult> pattern_finished;
  VideoSourceManager::LiveKitMethods methods;
  methods.create_and_publish = [&](const ros_portal_config::VideoSourceConfig& config)
      -> livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string> {
    ++create_calls;
    if (config.track_name == "broken") {
      return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::failure("pipeline failed");
    }
    auto session = std::make_shared<VideoSourceManager::Session>();
    session->start = [&start_calls] { ++start_calls; };
    session->stop = [&stop_calls] { ++stop_calls; };
    session->unpublish = [&unpublish_calls] { ++unpublish_calls; };
    session->poll_result = [&, name = config.track_name]() -> std::optional<VideoSourceResult> {
      if (name != "pattern") {
        return std::nullopt;
      }
      return std::exchange(pattern_finished, std::nullopt);
    };
    return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::success(std::move(session));
  };

  {
    const std::vector configs{
        makeSource("broken", ros_portal_config::CaptureSourceType::Gstreamer),
        makeSource("pattern", ros_portal_config::CaptureSourceType::Pattern),
        makeSource("camera", ros_portal_config::CaptureSourceType::Gstreamer),
    };
    const VideoSourceManager manager(configs, std::move(methods), std::move(diagnostics),
                                     rclcpp::get_logger("video_source_manager_test"));

    EXPECT_EQ(create_calls, 3);
    EXPECT_EQ(start_calls, 2);
    ASSERT_EQ(diagnostic_callbacks.size(), 3u);

    diagnostic_updater::DiagnosticStatusWrapper failed_status;
    diagnostic_callbacks.at("video_source/broken/0")(failed_status);
    EXPECT_EQ(failed_status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
    EXPECT_EQ(valueFor(failed_status, "state"), "error");
    EXPECT_EQ(valueFor(failed_status, "error"), "pipeline failed");

    pattern_finished = VideoSourceResult{std::nullopt, 42, VideoSourceExit::EndOfStream};
    diagnostic_updater::DiagnosticStatusWrapper finished_status;
    diagnostic_callbacks.at("video_source/pattern/1")(finished_status);
    EXPECT_EQ(finished_status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
    EXPECT_EQ(valueFor(finished_status, "state"), "end_of_stream");
    EXPECT_EQ(valueFor(finished_status, "frames_captured"), "42");

    // A consumed join result must preserve the terminal state on later updates.
    diagnostic_updater::DiagnosticStatusWrapper repeated_status;
    diagnostic_callbacks.at("video_source/pattern/1")(repeated_status);
    EXPECT_EQ(valueFor(repeated_status, "state"), "end_of_stream");
    EXPECT_EQ(valueFor(repeated_status, "frames_captured"), "42");

    // A failed sibling does not affect a healthy source of the same type.
    diagnostic_updater::DiagnosticStatusWrapper running_status;
    diagnostic_callbacks.at("video_source/camera/2")(running_status);
    EXPECT_EQ(running_status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
    EXPECT_EQ(valueFor(running_status, "state"), "running");
    EXPECT_EQ(valueFor(running_status, "source_type"), "gstreamer");
  }

  EXPECT_TRUE(diagnostic_callbacks.empty());
  EXPECT_EQ(stop_calls, 2);
  EXPECT_EQ(unpublish_calls, 2);
}

TEST(VideoSourceManagerTest, WaitsForCaptureCompletionBeforeUnpublishing) {
  capture::CaptureCompletion completion;
  std::promise<capture::CaptureCompletion::Result> result;
  completion.setResult(result.get_future());
  std::promise<void> stop_requested;
  std::atomic<bool> unpublished{false};
  diagnostics::DiagnosticsManagerFns diagnostics;
  diagnostics.add = [](const std::string&, auto) {};
  diagnostics.remove = [](const std::string&) {};
  VideoSourceManager::LiveKitMethods methods;
  methods.create_and_publish = [&](const auto&) {
    auto session = std::make_shared<VideoSourceManager::Session>();
    session->start = [] {};
    session->stop = [&] {
      stop_requested.set_value();
      completion.wait(std::chrono::seconds(5));
    };
    session->unpublish = [&] { unpublished.store(true); };
    session->poll_result = [] { return std::optional<VideoSourceResult>{}; };
    return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::success(std::move(session));
  };
  auto manager = std::make_unique<VideoSourceManager>(
      std::vector{makeSource("capture", ros_portal_config::CaptureSourceType::Gstreamer)}, std::move(methods),
      std::move(diagnostics), rclcpp::get_logger("video_source_shutdown_test"));
  auto stopped = stop_requested.get_future();
  auto shutdown = std::async(std::launch::async, [&] { manager.reset(); });
  EXPECT_EQ(stopped.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_FALSE(unpublished.load());
  EXPECT_EQ(shutdown.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  result.set_value(capture::CaptureCompletion::Result::success(livekit::capture::CaptureResult{}));
  EXPECT_NO_THROW(shutdown.get());
  EXPECT_TRUE(unpublished.load());
}

TEST(VideoSourceManagerTest, UnpublishesWhenStopThrows) {
  std::vector<std::string> events;
  diagnostics::DiagnosticsManagerFns diagnostics;
  diagnostics.add = [](const std::string&, auto) {};
  diagnostics.remove = [&events](const std::string&) { events.emplace_back("remove diagnostics"); };
  VideoSourceManager::LiveKitMethods methods;
  methods.create_and_publish = [&events](const auto&) {
    auto session = std::make_shared<VideoSourceManager::Session>();
    session->start = [] {};
    session->stop = [&events] {
      events.emplace_back("capture completion failed");
      throw std::runtime_error("capture join failed");
    };
    session->unpublish = [&events] { events.emplace_back("unpublish"); };
    session->poll_result = [] { return std::optional<VideoSourceResult>{}; };
    return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::success(std::move(session));
  };
  {
    const VideoSourceManager manager({makeSource("capture", ros_portal_config::CaptureSourceType::Gstreamer)},
                                     std::move(methods), std::move(diagnostics),
                                     rclcpp::get_logger("video_source_shutdown_test"));
  }
  EXPECT_EQ(events, (std::vector<std::string>{"remove diagnostics", "capture completion failed", "unpublish"}));
}

class VideoSourceTerminalTest : public ::testing::TestWithParam<std::string> {};

TEST_P(VideoSourceTerminalTest, PollsTerminalResultWithoutBlockingOrLosingErrors) {
  diagnostics::DiagnosticsManagerFns::TaskCallback diagnostic;
  diagnostics::DiagnosticsManagerFns diagnostics;
  diagnostics.add = [&diagnostic](const std::string&, auto callback) { diagnostic = std::move(callback); };
  diagnostics.remove = [&diagnostic](const std::string&) { diagnostic = {}; };

  int polls = 0;
  std::optional<VideoSourceResult> terminal;
  bool join_throws = false;
  VideoSourceManager::LiveKitMethods methods;
  methods.create_and_publish = [&](const auto&) {
    auto session = std::make_shared<VideoSourceManager::Session>();
    session->start = [] {};
    session->stop = [] {};
    session->unpublish = [] {};
    session->poll_result = [&]() -> std::optional<VideoSourceResult> {
      ++polls;
      if (std::exchange(join_throws, false)) {
        throw std::runtime_error("capture join transport failure");
      }
      return std::exchange(terminal, std::nullopt);
    };
    return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::success(std::move(session));
  };
  const VideoSourceManager manager({makeSource("capture", ros_portal_config::CaptureSourceType::Gstreamer)},
                                   std::move(methods), std::move(diagnostics),
                                   rclcpp::get_logger("video_source_terminal_test"));

  diagnostic_updater::DiagnosticStatusWrapper running;
  diagnostic(running);
  EXPECT_EQ(valueFor(running, "state"), "running");
  EXPECT_EQ(polls, 1);

  if (GetParam() == "stopped") {
    terminal = VideoSourceResult{std::nullopt, 12, VideoSourceExit::Stopped};
  } else if (GetParam() == "error") {
    terminal = VideoSourceResult{"capture source failed", 0, VideoSourceExit::Stopped};
  } else {
    join_throws = true;
  }
  diagnostic_updater::DiagnosticStatusWrapper finished;
  diagnostic(finished);
  EXPECT_EQ(polls, 2);
  if (GetParam() == "stopped") {
    EXPECT_EQ(finished.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
    EXPECT_EQ(valueFor(finished, "state"), "stopped");
    EXPECT_EQ(valueFor(finished, "frames_captured"), "12");
  } else {
    EXPECT_EQ(finished.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
    EXPECT_EQ(valueFor(finished, "state"), "error");
    EXPECT_EQ(valueFor(finished, "error"),
              GetParam() == "error" ? "capture source failed" : "capture join transport failure");
  }
  diagnostic_updater::DiagnosticStatusWrapper repeated;
  diagnostic(repeated);
  EXPECT_EQ(repeated.message, finished.message);
  EXPECT_EQ(valueFor(repeated, "state"), valueFor(finished, "state"));
}

INSTANTIATE_TEST_SUITE_P(CaptureJoin, VideoSourceTerminalTest, ::testing::Values("stopped", "error", "exception"));

TEST(VideoSourceManagerTest, RendersCaptureError) {
  VideoSourceState state;
  state.track_name = "front_camera";
  state.source_type = "gstreamer";
  state.kind = VideoSourceStateKind::Error;
  state.frames_captured = 17;
  state.error = "GStreamer bus error";

  diagnostic_updater::DiagnosticStatusWrapper status;
  populateVideoSourceStatus(state, status);

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(status.message, "GStreamer bus error");
  EXPECT_EQ(valueFor(status, "track_name"), "front_camera");
  EXPECT_EQ(valueFor(status, "source_type"), "gstreamer");
  EXPECT_EQ(valueFor(status, "frames_captured"), "17");
}

} // namespace
} // namespace ros_portal
