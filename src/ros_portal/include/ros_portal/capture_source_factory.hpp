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

#include <livekit/capture_source.h>
// room_event_types.h carries TrackPublishOptions/VideoEncodingOptions but only
// forward-declares TrackSource, whose enumerators live in track.h.
#include <livekit/room_event_types.h>
#include <livekit/track.h>

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "ros_portal_config/config/config_parser.hpp"

/// @brief Translation from validated ROS Portal config into LiveKit capture
/// source configurations.
///
/// The config schema models capture sources as a union tagged by
/// @c source.type, but the generator backing it supports neither @c oneOf nor
/// conditional subschemas. Every per-type rule ("a device source requires a
/// @c device block", "@c highest_framerate rejects @c framerate_fps") is
/// therefore enforced here rather than by the parser, and reported as
/// @c std::invalid_argument.
namespace ros_portal::capture {

/// Output characteristics used when a pattern source omits them.
///
/// The SDK requires both to be non-zero (see @c PatternVideoSourceConfig in
/// livekit/capture_source.h), so an omitted @c pattern block cannot simply
/// forward a default-constructed config.
inline constexpr int kDefaultPatternWidth = 640;
inline constexpr int kDefaultPatternHeight = 480;
inline constexpr std::uint32_t kDefaultPatternFramerateFps = 30;

/// @brief Inspectable, validated form of a configured device capture request.
///
/// @c livekit::DeviceSelector and @c livekit::DeviceFormatRequest keep their
/// state private and befriend only @c livekit::CaptureSource, so a caller
/// cannot read back what a constructed @c DeviceVideoSourceConfig holds.
/// Validation results land here instead, and @ref toLiveKitConfig performs the
/// final, logic-free conversion to SDK types. This keeps the interesting part
/// (validation and defaulting) testable.
struct DeviceRequest {
  /// How the device to open is identified.
  enum class Selection {
    /// The platform default device.
    Default,
    /// By position in the platform's own enumeration order.
    Index,
    /// By platform-stable identifier.
    Id,
  };

  /// How the delivered capture format is chosen.
  enum class Strategy {
    /// Let the device pick its default format.
    Default,
    /// Require the requested resolution and frame rate.
    Exact,
    /// Use the device's nearest supported resolution and frame rate.
    Closest,
    /// Maximize frame rate, optionally constrained.
    HighestFramerate,
    /// Maximize resolution, optionally constrained.
    HighestResolution,
  };

  Selection selection{Selection::Default};
  /// Meaningful only when @ref selection is @c Selection::Index.
  std::uint32_t index{0};
  /// Meaningful only when @ref selection is @c Selection::Id.
  std::string id;

  Strategy strategy{Strategy::Default};
  std::optional<livekit::CaptureResolution> resolution;
  std::optional<std::uint32_t> framerate_fps;
  /// Left unset for the @c Highest* strategies unless the config names one,
  /// because naming a format there selects it outright with no fallback.
  std::optional<livekit::DeviceFrameFormat> frame_format;
};

/// @brief Map a configured codec onto its SDK enumerator.
livekit::VideoCodec toLiveKitCodec(ros_portal_config::VideoCodec codec);

/// @brief Map a configured pixel format onto its SDK enumerator.
livekit::DeviceFrameFormat toLiveKitFrameFormat(ros_portal_config::DeviceFrameFormat format);

/// @brief Map a configured test pattern onto its SDK enumerator.
livekit::Pattern toLiveKitPattern(ros_portal_config::VideoPattern pattern);

/// @brief Translate a validated GStreamer capture source entry.
/// @throws std::invalid_argument when the entry is not a valid GStreamer source.
livekit::GstreamerVideoSourceConfig toGstreamerConfig(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Translate a validated pattern capture source entry.
/// @throws std::invalid_argument when the entry is not a valid pattern source.
livekit::PatternVideoSourceConfig toPatternConfig(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Validate a device capture source entry into its inspectable form.
/// @throws std::invalid_argument when the entry is not a valid device source.
DeviceRequest toDeviceRequest(const ros_portal_config::CaptureSourceConfig& source);

/// @brief Convert a validated device request into the SDK configuration.
livekit::DeviceVideoSourceConfig toLiveKitConfig(const DeviceRequest& request);

/// @brief Dispatch on @c source.type and hand the translated config to the SDK.
/// @throws std::invalid_argument when the entry is invalid for its type.
std::future<std::shared_ptr<livekit::CaptureSource>> createCaptureSource(
    const ros_portal_config::CaptureSourceConfig& source);

/// @brief Build camera track publish options from the configured overrides.
/// @param configured Optional application-controlled publish limits.
/// @param simulcast  Whether to request simulcast for the track. Encoded sources dictate
///                   their own setting and override this in @c CaptureSource::publishOptions().
livekit::TrackPublishOptions capturePublishOptions(
    const std::optional<ros_portal_config::VideoPublishOptions>& configured, bool simulcast = false);

} // namespace ros_portal::capture
