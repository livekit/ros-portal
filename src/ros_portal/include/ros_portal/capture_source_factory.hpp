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

#include <livekit/capture/capture_source.h>
// room_event_types.h carries TrackPublishOptions/VideoEncodingOptions but only
// forward-declares TrackSource, whose enumerators live in track.h.
#include <livekit/room_event_types.h>
#include <livekit/track.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "ros_portal_config/config/config_parser.hpp"

/// @brief Translation from validated ROS Portal config into LiveKit capture
/// source configurations.
///
/// The config schema models capture sources as a union tagged by
/// @c source.type, but the generator backing it supports neither @c oneOf nor
/// conditional subschemas. Every per-type rule ("a gstreamer source requires a
/// @c pipeline", "a pattern source rejects gstreamer fields") is therefore
/// enforced here rather than by the parser, and reported as
/// @c std::invalid_argument.
namespace ros_portal::capture {

/// Output characteristics used only when a pattern or clock source omits them.
///
/// These synthetic sources generate their own frames and need output settings.
/// GStreamer sources discover omitted characteristics from negotiated caps.
inline constexpr int kDefaultPixelWidth = 640;
inline constexpr int kDefaultPixelHeight = 480;
inline constexpr std::uint32_t kDefaultPixelFramerateFps = 30;

/// @brief Share one SDK join future between diagnostics and shutdown.
/// All methods are thread-safe. Shutdown consumes the future under the mutex,
/// then waits outside the lock so diagnostics remain nonblocking.
class CaptureCompletion final {
public:
  using Result = livekit::Result<livekit::capture::CaptureResult, livekit::capture::CaptureError>;

  /// @brief Store the single join future after a successful capture start.
  void setResult(std::future<Result> result);
  /// @brief Consume a ready result without blocking; return no value otherwise.
  std::optional<Result> poll();
  /// @brief Consume the remaining result after requesting stop, if still valid.
  /// @param timeout Maximum time to wait for capture completion.
  /// @throws std::runtime_error on timeout or a capture error; propagates future exceptions.
  void wait(std::chrono::milliseconds timeout);

private:
  std::mutex mutex_;
  std::future<Result> result_;
};

/// @brief Map a configured codec onto its SDK enumerator.
livekit::VideoCodec toLiveKitCodec(ros_portal_config::VideoCodec codec);

/// @brief Map a configured test pattern onto its SDK enumerator.
livekit::capture::Pattern toLiveKitPattern(ros_portal_config::VideoPattern pattern);

/// @brief Translate a validated GStreamer capture source entry.
/// @throws std::invalid_argument when the entry is not a valid GStreamer source.
livekit::capture::GstreamerVideoSourceConfig toGstreamerConfig(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Translate a validated pattern capture source entry.
/// @throws std::invalid_argument when the entry is not a valid pattern source.
livekit::capture::PatternVideoSourceConfig toPatternConfig(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Translate a validated clock capture source entry.
/// @throws std::invalid_argument when the entry is not a valid clock source.
livekit::capture::ClockVideoSourceConfig toClockConfig(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Dispatch on @c source.type and hand the translated config to the SDK.
/// @return Future containing the SDK source Result; inspect it before accessing the source.
/// @throws std::runtime_error when awaiting the future if SDK setup fails.
/// @throws std::invalid_argument when the entry is invalid for its type.
std::future<livekit::Result<std::shared_ptr<livekit::capture::CaptureSource>, livekit::capture::CaptureError>>
createCaptureSource(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Build camera track publish options from the configured overrides.
/// @param configured Optional application-controlled publish limits.
/// @param simulcast  Whether to request simulcast for the track. Encoded sources dictate
///                   their own setting and override this in @c CaptureSource::publishOptions().
livekit::TrackPublishOptions capturePublishOptions(
    const std::optional<ros_portal_config::VideoPublishOptions>& configured, bool simulcast = false);

} // namespace ros_portal::capture
