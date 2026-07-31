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

#include <livekit/result.h>

#include <cstdint>
#include <diagnostic_updater/diagnostic_status_wrapper.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/logger.hpp>
#include <string>
#include <vector>

#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal_config/config/config_parser.hpp"

namespace ros_portal {

/// @brief Terminal reason reported by a configured video capture source.
enum class VideoSourceExit {
  /// Capture was explicitly stopped.
  Stopped,
  /// The source reached the end of its stream.
  EndOfStream,
};

/// @brief Result delivered when a configured capture source terminates.
struct VideoSourceResult {
  /// Error text when capture failed.
  std::optional<std::string> error;
  /// Number of frames or encoded access units captured.
  std::uint64_t frames_captured{0};
  /// Normal terminal reason; meaningful only when @ref error is empty.
  VideoSourceExit exit{VideoSourceExit::Stopped};
};

/// @brief Runtime state rendered by one configured video-source diagnostic.
enum class VideoSourceStateKind {
  /// Source construction or publication is in progress.
  Starting,
  /// Source is published and capturing.
  Running,
  /// Source was explicitly stopped.
  Stopped,
  /// Source reached end-of-stream.
  EndOfStream,
  /// Source construction, publication, start, or capture failed.
  Error,
};

/// @brief Thread-safe state behind one configured video source.
struct VideoSourceState {
  /// LiveKit track name from configuration.
  std::string track_name;
  /// Stable configured source type name.
  std::string source_type;
  /// Protects mutable fields updated by SDK callbacks.
  mutable std::mutex mutex;
  /// Current lifecycle state.
  VideoSourceStateKind kind{VideoSourceStateKind::Starting};
  /// Latest terminal frame count.
  std::uint64_t frames_captured{0};
  /// Latest failure text.
  std::string error;
};

/// @brief Populate a diagnostic status from one video-source state snapshot.
/// @param state Source state to render.
/// @param status Diagnostic status to populate.
void populateVideoSourceStatus(const VideoSourceState& state, diagnostic_updater::DiagnosticStatusWrapper& status);

/// @brief Owns configured LiveKit capture sources and their diagnostics.
class VideoSourceManager final {
public:
  /// @brief Type-erased published capture session.
  struct Session {
    /// Start capture after the track has been published.
    std::function<void()> start;
    /// Stop a running capture.
    std::function<void()> stop;
    /// Unpublish the capture-backed track.
    std::function<void()> unpublish;
  };

  /// @brief Callback invoked exactly once when a started capture terminates.
  using FinishedCallback = std::function<void(const VideoSourceResult&)>;

  /// @brief LiveKit operations owned by the ROS Portal edge.
  struct LiveKitMethods {
    /// Create and publish one configured source, installing @p callback before
    /// capture starts.
    std::function<livekit::Result<std::shared_ptr<Session>, std::string>(const ros_portal_config::VideoSourceConfig&,
                                                                         FinishedCallback)>
        create_and_publish;
  };

  /// @brief Construct and start every configured video source.
  ///
  /// A failure is isolated to its source and recorded in diagnostics; it does
  /// not prevent later sources from starting.
  /// @param configs Video sources parsed from ROS Portal configuration.
  /// @param livekit_methods LiveKit session factory.
  /// @param diagnostics ROS Portal-owned diagnostic registration functions.
  /// @param logger ROS logger used for lifecycle messages.
  /// @throws std::invalid_argument when required callbacks are incomplete.
  VideoSourceManager(const std::vector<ros_portal_config::VideoSourceConfig>& configs, LiveKitMethods livekit_methods,
                     diagnostics::DiagnosticsManagerFns diagnostics, rclcpp::Logger logger);

  /// @brief Stop, unpublish, and release every successfully created source.
  ~VideoSourceManager();

  VideoSourceManager(const VideoSourceManager&) = delete;
  VideoSourceManager& operator=(const VideoSourceManager&) = delete;

private:
  struct Runtime {
    std::string diagnostic_name;
    std::shared_ptr<VideoSourceState> state;
    std::shared_ptr<Session> session;
  };

  /// Record and log a source-local startup failure.
  void markError(const std::shared_ptr<VideoSourceState>& state, const std::string& error) const;

  LiveKitMethods livekit_methods_;
  diagnostics::DiagnosticsManagerFns diagnostics_;
  rclcpp::Logger logger_;
  std::vector<Runtime> runtimes_;
};

} // namespace ros_portal
