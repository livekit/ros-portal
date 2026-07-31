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

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <map>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <utility>
#include <vector>

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
  if (type == ros_portal_config::CaptureSourceType::Device) {
    // The manager never inspects the block, but keep the fixture realistic.
    config.source.device = ros_portal_config::DeviceSourceConfig{};
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
  VideoSourceManager::FinishedCallback demo_finished;
  VideoSourceManager::LiveKitMethods methods;
  methods.create_and_publish = [&](const ros_portal_config::VideoSourceConfig& config,
                                   VideoSourceManager::FinishedCallback callback)
      -> livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string> {
    ++create_calls;
    if (config.track_name == "broken") {
      return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::failure("pipeline failed");
    }
    if (config.track_name == "demo") {
      demo_finished = std::move(callback);
    }
    auto session = std::make_shared<VideoSourceManager::Session>();
    session->start = [&start_calls] { ++start_calls; };
    session->stop = [&stop_calls] { ++stop_calls; };
    session->unpublish = [&unpublish_calls] { ++unpublish_calls; };
    return livekit::Result<std::shared_ptr<VideoSourceManager::Session>, std::string>::success(std::move(session));
  };

  {
    const std::vector configs{
        makeSource("broken", ros_portal_config::CaptureSourceType::Gstreamer),
        makeSource("demo", ros_portal_config::CaptureSourceType::Demo),
        makeSource("usb_camera", ros_portal_config::CaptureSourceType::Device),
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

    ASSERT_TRUE(static_cast<bool>(demo_finished));
    demo_finished(VideoSourceResult{std::nullopt, 42, VideoSourceExit::EndOfStream});
    diagnostic_updater::DiagnosticStatusWrapper finished_status;
    diagnostic_callbacks.at("video_source/demo/1")(finished_status);
    EXPECT_EQ(finished_status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
    EXPECT_EQ(valueFor(finished_status, "state"), "end_of_stream");
    EXPECT_EQ(valueFor(finished_status, "frames_captured"), "42");

    // A device source needs no manager changes: source_type is rendered from
    // the config enum, so the new variant flows into diagnostics for free.
    diagnostic_updater::DiagnosticStatusWrapper device_status;
    diagnostic_callbacks.at("video_source/usb_camera/2")(device_status);
    EXPECT_EQ(device_status.level, diagnostic_msgs::msg::DiagnosticStatus::OK);
    EXPECT_EQ(valueFor(device_status, "state"), "running");
    EXPECT_EQ(valueFor(device_status, "source_type"), "device");
  }

  EXPECT_TRUE(diagnostic_callbacks.empty());
  EXPECT_EQ(stop_calls, 2);
  EXPECT_EQ(unpublish_calls, 2);
}

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
