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

#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <exception>
#include <stdexcept>
#include <utility>

namespace ros_portal {

namespace {

const char* stateName(VideoSourceStateKind kind) {
  switch (kind) {
    case VideoSourceStateKind::Starting:
      return "starting";
    case VideoSourceStateKind::Running:
      return "running";
    case VideoSourceStateKind::Stopped:
      return "stopped";
    case VideoSourceStateKind::EndOfStream:
      return "end_of_stream";
    case VideoSourceStateKind::Error:
      return "error";
  }
  return "error";
}

} // namespace

void populateVideoSourceStatus(const VideoSourceState& state, diagnostic_updater::DiagnosticStatusWrapper& status) {
  const std::lock_guard<std::mutex> lock(state.mutex);
  switch (state.kind) {
    case VideoSourceStateKind::Running:
      status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Video source is running");
      break;
    case VideoSourceStateKind::Stopped:
      status.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Video source stopped");
      break;
    case VideoSourceStateKind::Starting:
      status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Video source is starting");
      break;
    case VideoSourceStateKind::EndOfStream:
      status.summary(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Video source reached end-of-stream");
      break;
    case VideoSourceStateKind::Error:
      status.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                     state.error.empty() ? "Video source failed" : state.error);
      break;
  }
  status.add("track_name", state.track_name);
  status.add("source_type", state.source_type);
  status.add("state", stateName(state.kind));
  status.add("frames_captured", std::to_string(state.frames_captured));
  status.add("error", state.error);
}

VideoSourceManager::VideoSourceManager(const std::vector<ros_portal_config::VideoSourceConfig>& configs,
                                       LiveKitMethods livekit_methods, diagnostics::DiagnosticsManagerFns diagnostics,
                                       rclcpp::Logger logger)
    : livekit_methods_(std::move(livekit_methods)), diagnostics_(std::move(diagnostics)), logger_(std::move(logger)) {
  if (!livekit_methods_.create_and_publish) {
    throw std::invalid_argument("VideoSourceManager requires create_and_publish");
  }
  if (!diagnostics_.add || !diagnostics_.remove) {
    throw std::invalid_argument("VideoSourceManager requires complete diagnostics functions");
  }

  runtimes_.reserve(configs.size());
  for (std::size_t index = 0; index < configs.size(); ++index) {
    const auto& config = configs[index];
    auto state = std::make_shared<VideoSourceState>();
    state->track_name = config.track_name;
    state->source_type = ros_portal_config::toString(config.source.type);

    Runtime runtime;
    runtime.diagnostic_name = "video_source/" + config.track_name + "/" + std::to_string(index);
    runtime.state = state;
    diagnostics_.add(runtime.diagnostic_name, [state](auto& status) { populateVideoSourceStatus(*state, status); });
    const auto cleanup_failed_session = [&runtime, &state, this] {
      if (!runtime.session) {
        return;
      }
      try {
        runtime.session->stop();
      } catch (const std::exception& error) {
        RCLCPP_WARN(logger_, "Cleanup could not stop video source '%s': %s", state->track_name.c_str(), error.what());
      } catch (...) {
        RCLCPP_WARN(logger_, "Cleanup could not stop video source '%s': unknown error", state->track_name.c_str());
      }
      try {
        runtime.session->unpublish();
      } catch (const std::exception& error) {
        RCLCPP_WARN(logger_, "Cleanup could not unpublish video source '%s': %s", state->track_name.c_str(),
                    error.what());
      } catch (...) {
        RCLCPP_WARN(logger_, "Cleanup could not unpublish video source '%s': unknown error", state->track_name.c_str());
      }
      runtime.session.reset();
    };

    try {
      auto result =
          livekit_methods_.create_and_publish(config, [state, logger = logger_](const VideoSourceResult& finished) {
            {
              const std::lock_guard<std::mutex> lock(state->mutex);
              state->frames_captured = finished.frames_captured;
              if (finished.error) {
                state->kind = VideoSourceStateKind::Error;
                state->error = *finished.error;
              } else {
                state->kind = finished.exit == VideoSourceExit::EndOfStream ? VideoSourceStateKind::EndOfStream
                                                                            : VideoSourceStateKind::Stopped;
                state->error.clear();
              }
            }
            if (finished.error) {
              RCLCPP_ERROR(logger, "Video source '%s' failed after %llu frames: %s", state->track_name.c_str(),
                           static_cast<unsigned long long>(finished.frames_captured), finished.error->c_str());
            } else {
              RCLCPP_INFO(logger, "Video source '%s' finished after %llu frames (%s)", state->track_name.c_str(),
                          static_cast<unsigned long long>(finished.frames_captured),
                          finished.exit == VideoSourceExit::EndOfStream ? "end-of-stream" : "stopped");
            }
          });
      if (!result) {
        markError(state, result.error());
      } else if (!result.value() || !result.value()->start || !result.value()->stop || !result.value()->unpublish) {
        markError(state, "create_and_publish returned an invalid capture session");
      } else {
        runtime.session = result.value();
        {
          const std::lock_guard<std::mutex> lock(state->mutex);
          state->kind = VideoSourceStateKind::Running;
          state->error.clear();
        }
        runtime.session->start();
        {
          const std::lock_guard<std::mutex> lock(state->mutex);
          if (state->kind == VideoSourceStateKind::Running) {
            RCLCPP_INFO(logger_, "Started %s video source '%s'", state->source_type.c_str(), state->track_name.c_str());
          }
        }
      }
    } catch (const std::exception& error) {
      cleanup_failed_session();
      markError(state, error.what());
    } catch (...) {
      cleanup_failed_session();
      markError(state, "unknown error");
    }
    runtimes_.push_back(std::move(runtime));
  }
}

VideoSourceManager::~VideoSourceManager() {
  for (auto& runtime : runtimes_) {
    diagnostics_.remove(runtime.diagnostic_name);
  }

  for (auto it = runtimes_.rbegin(); it != runtimes_.rend(); ++it) {
    if (!it->session) {
      continue;
    }
    try {
      it->session->stop();
    } catch (const std::exception& error) {
      RCLCPP_WARN(logger_, "Failed to stop video source '%s': %s", it->state->track_name.c_str(), error.what());
    }
    try {
      it->session->unpublish();
    } catch (const std::exception& error) {
      RCLCPP_WARN(logger_, "Failed to unpublish video source '%s': %s", it->state->track_name.c_str(), error.what());
    }
  }
}

void VideoSourceManager::markError(const std::shared_ptr<VideoSourceState>& state, const std::string& error) const {
  {
    const std::lock_guard<std::mutex> lock(state->mutex);
    state->kind = VideoSourceStateKind::Error;
    state->error = error;
  }
  RCLCPP_ERROR(logger_, "Video source '%s' failed to start: %s", state->track_name.c_str(), error.c_str());
}

} // namespace ros_portal
