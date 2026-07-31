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

#include "ros_portal/capture_source_factory.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ros_portal::capture {

namespace {

/// Fields on a capture source entry that only a GStreamer source accepts.
std::vector<std::string> gstreamerOnlyFieldsPresent(const ros_portal_config::CaptureSourceConfig& source) {
  std::vector<std::string> present;
  if (source.pipeline) {
    present.emplace_back("pipeline");
  }
  if (source.codec) {
    present.emplace_back("codec");
  }
  if (source.resolution) {
    present.emplace_back("resolution");
  }
  if (source.rate_control) {
    present.emplace_back("rate_control");
  }
  return present;
}

std::string join(const std::vector<std::string>& values) {
  std::string joined;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      joined += ", ";
    }
    joined += values[i];
  }
  return joined;
}

/// Reject the GStreamer-only fields on a source type that does not accept them.
void rejectGstreamerOnlyFields(const ros_portal_config::CaptureSourceConfig& source, const char* type_name) {
  const auto present = gstreamerOnlyFieldsPresent(source);
  if (!present.empty()) {
    throw std::invalid_argument(std::string(type_name) + " source does not accept gstreamer options (" + join(present) +
                                ")");
  }
}

/// @c framerate_fps is bounded positive by the schema (@c positiveInteger), so
/// narrowing is safe here.
std::uint32_t toUnsigned(int value) { return static_cast<std::uint32_t>(value); }

livekit::CaptureResolution toLiveKitResolution(const ros_portal_config::CaptureResolution& resolution) {
  return livekit::CaptureResolution{resolution.width, resolution.height};
}

} // namespace

livekit::VideoCodec toLiveKitCodec(ros_portal_config::VideoCodec codec) {
  switch (codec) {
    case ros_portal_config::VideoCodec::H264:
      return livekit::VideoCodec::H264;
    case ros_portal_config::VideoCodec::H265:
      return livekit::VideoCodec::H265;
    case ros_portal_config::VideoCodec::Vp8:
      return livekit::VideoCodec::VP8;
    case ros_portal_config::VideoCodec::Vp9:
      return livekit::VideoCodec::VP9;
    case ros_portal_config::VideoCodec::Av1:
      return livekit::VideoCodec::AV1;
  }
  return livekit::VideoCodec::H264;
}

livekit::Pattern toLiveKitPattern(ros_portal_config::VideoPattern pattern) {
  switch (pattern) {
    case ros_portal_config::VideoPattern::Gradient:
      return livekit::Pattern::Gradient;
    case ros_portal_config::VideoPattern::Logo:
      return livekit::Pattern::Logo;
  }
  return livekit::Pattern::Gradient;
}

livekit::GstreamerVideoSourceConfig toGstreamerConfig(const ros_portal_config::CaptureSourceConfig& source) {
  if (source.pattern) {
    throw std::invalid_argument("gstreamer source does not accept a 'pattern' block");
  }

  if (!source.pipeline) {
    throw std::invalid_argument("gstreamer source requires a pipeline");
  }

  livekit::GstreamerVideoSourceConfig config;
  config.pipeline = *source.pipeline;
  if (source.codec) {
    config.codec = toLiveKitCodec(*source.codec);
  }
  if (source.resolution) {
    config.resolution = toLiveKitResolution(*source.resolution);
  }
  if (source.rate_control) {
    const auto& rate_control = *source.rate_control;
    config.rate_control = livekit::GstreamerRateControl{
        rate_control.element,
        rate_control.property,
        rate_control.unit == ros_portal_config::GstreamerBitrateUnit::Kbps ? livekit::GstreamerBitrateUnit::Kbps
                                                                           : livekit::GstreamerBitrateUnit::Bps,
    };
  }
  return config;
}

livekit::PatternVideoSourceConfig toPatternConfig(const ros_portal_config::CaptureSourceConfig& source) {
  rejectGstreamerOnlyFields(source, "pattern");

  // The SDK rejects zero resolution or frame rate, so fill in defaults rather
  // than forwarding a default-constructed config.
  livekit::PatternVideoSourceConfig config;
  config.resolution = livekit::CaptureResolution{kDefaultPatternWidth, kDefaultPatternHeight};
  config.framerate_fps = kDefaultPatternFramerateFps;

  if (source.pattern) {
    if (source.pattern->pattern) {
      config.pattern = toLiveKitPattern(*source.pattern->pattern);
    }
    if (source.pattern->resolution) {
      config.resolution = toLiveKitResolution(*source.pattern->resolution);
    }
    if (source.pattern->framerate_fps) {
      config.framerate_fps = toUnsigned(*source.pattern->framerate_fps);
    }
  }
  return config;
}

std::future<std::shared_ptr<livekit::CaptureSource>> createCaptureSource(
    const ros_portal_config::CaptureSourceConfig& source) {
  switch (source.type) {
    case ros_portal_config::CaptureSourceType::Pattern:
      return livekit::CaptureSource::create(toPatternConfig(source));
    case ros_portal_config::CaptureSourceType::Gstreamer:
      break;
  }
  return livekit::CaptureSource::create(toGstreamerConfig(source));
}

livekit::TrackPublishOptions capturePublishOptions(
    const std::optional<ros_portal_config::VideoPublishOptions>& configured, bool simulcast) {
  livekit::TrackPublishOptions options;
  options.source = livekit::TrackSource::SOURCE_CAMERA;
  options.simulcast = simulcast;
  if (!configured) {
    return options;
  }

  livekit::VideoEncodingOptions encoding;
  bool has_encoding = false;
  if (configured->max_bitrate_bps) {
    encoding.max_bitrate = static_cast<std::uint64_t>(*configured->max_bitrate_bps);
    has_encoding = true;
  }
  if (configured->max_framerate) {
    encoding.max_framerate = static_cast<double>(*configured->max_framerate);
    has_encoding = true;
  }
  if (has_encoding) {
    options.video_encoding = encoding;
  }
  return options;
}

} // namespace ros_portal::capture
