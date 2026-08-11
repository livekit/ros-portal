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

/// Reject a sibling type's configuration block.
void rejectForeignBlock(bool present, const char* type_name, const char* block_name) {
  if (present) {
    throw std::invalid_argument(std::string(type_name) + " source does not accept a '" + block_name + "' block");
  }
}

/// @c framerate_fps and @c index are bounded non-negative by the schema
/// (@c positiveInteger / @c nonNegativeInteger), so narrowing is safe here.
std::uint32_t toUnsigned(int value) { return static_cast<std::uint32_t>(value); }

livekit::CaptureResolution toLiveKitResolution(const ros_portal_config::CaptureResolution& resolution) {
  return livekit::CaptureResolution{resolution.width, resolution.height};
}

const char* strategyName(ros_portal_config::DeviceFormatStrategy strategy) {
  return ros_portal_config::toString(strategy);
}

/// Validate the format block and fold in the frame-format default.
///
/// Only @c exact and @c closest get a default: for the @c highest_* strategies
/// a named frame format selects the format outright with no fallback, so
/// defaulting one would silently narrow the candidate set.
void applyFormatSelection(const ros_portal_config::DeviceFormatSelection& selection, DeviceRequest& request) {
  const char* name = strategyName(selection.strategy);

  switch (selection.strategy) {
    case ros_portal_config::DeviceFormatStrategy::Exact:
    case ros_portal_config::DeviceFormatStrategy::Closest:
      if (!selection.resolution || !selection.framerate_fps) {
        throw std::invalid_argument(std::string("'") + name +
                                    "' device format requires both resolution and framerate_fps");
      }
      request.strategy = selection.strategy == ros_portal_config::DeviceFormatStrategy::Exact
                             ? DeviceRequest::Strategy::Exact
                             : DeviceRequest::Strategy::Closest;
      request.resolution = toLiveKitResolution(*selection.resolution);
      request.framerate_fps = toUnsigned(*selection.framerate_fps);
      request.frame_format =
          selection.frame_format ? toLiveKitFrameFormat(*selection.frame_format) : livekit::DeviceFrameFormat::Nv12;
      return;

    case ros_portal_config::DeviceFormatStrategy::HighestFramerate:
      if (selection.framerate_fps) {
        throw std::invalid_argument(std::string("'") + name +
                                    "' device format does not accept framerate_fps; it is the maximized axis");
      }
      request.strategy = DeviceRequest::Strategy::HighestFramerate;
      if (selection.resolution) {
        request.resolution = toLiveKitResolution(*selection.resolution);
      }
      break;

    case ros_portal_config::DeviceFormatStrategy::HighestResolution:
      if (selection.resolution) {
        throw std::invalid_argument(std::string("'") + name +
                                    "' device format does not accept resolution; it is the maximized axis");
      }
      request.strategy = DeviceRequest::Strategy::HighestResolution;
      if (selection.framerate_fps) {
        request.framerate_fps = toUnsigned(*selection.framerate_fps);
      }
      break;
  }

  // Deliberately not defaulted for the highest_* strategies; see above.
  if (selection.frame_format) {
    request.frame_format = toLiveKitFrameFormat(*selection.frame_format);
  }
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

livekit::DeviceFrameFormat toLiveKitFrameFormat(ros_portal_config::DeviceFrameFormat format) {
  switch (format) {
    case ros_portal_config::DeviceFrameFormat::I420:
      return livekit::DeviceFrameFormat::I420;
    case ros_portal_config::DeviceFrameFormat::Nv12:
      return livekit::DeviceFrameFormat::Nv12;
    case ros_portal_config::DeviceFrameFormat::Bgra:
      return livekit::DeviceFrameFormat::Bgra;
    case ros_portal_config::DeviceFrameFormat::Rgb24:
      return livekit::DeviceFrameFormat::Rgb24;
    case ros_portal_config::DeviceFrameFormat::Bgr24:
      return livekit::DeviceFrameFormat::Bgr24;
    case ros_portal_config::DeviceFrameFormat::Yuyv:
      return livekit::DeviceFrameFormat::Yuyv;
    case ros_portal_config::DeviceFrameFormat::Uyvy:
      return livekit::DeviceFrameFormat::Uyvy;
    case ros_portal_config::DeviceFrameFormat::Grey:
      return livekit::DeviceFrameFormat::Grey;
    case ros_portal_config::DeviceFrameFormat::Mjpeg:
      return livekit::DeviceFrameFormat::Mjpeg;
  }
  return livekit::DeviceFrameFormat::Nv12;
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
  rejectForeignBlock(source.device.has_value(), "gstreamer", "device");
  rejectForeignBlock(source.pattern.has_value(), "gstreamer", "pattern");

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
  rejectForeignBlock(source.device.has_value(), "pattern", "device");

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

DeviceRequest toDeviceRequest(const ros_portal_config::CaptureSourceConfig& source) {
  rejectGstreamerOnlyFields(source, "device");
  rejectForeignBlock(source.pattern.has_value(), "device", "pattern");

  if (!source.device) {
    throw std::invalid_argument("device source requires a 'device' block");
  }
  const auto& device = *source.device;

  if (device.index && device.id) {
    throw std::invalid_argument("device selection accepts at most one of 'index' or 'id'");
  }

  DeviceRequest request;
  if (device.index) {
    request.selection = DeviceRequest::Selection::Index;
    request.index = toUnsigned(*device.index);
  } else if (device.id) {
    request.selection = DeviceRequest::Selection::Id;
    request.id = *device.id;
  }

  if (device.format) {
    applyFormatSelection(*device.format, request);
  }
  return request;
}

livekit::DeviceVideoSourceConfig toLiveKitConfig(const DeviceRequest& request) {
  livekit::DeviceVideoSourceConfig config;

  switch (request.selection) {
    case DeviceRequest::Selection::Default:
      break; // default-constructed selector opens the platform default device
    case DeviceRequest::Selection::Index:
      config.device = livekit::DeviceSelector::index(request.index);
      break;
    case DeviceRequest::Selection::Id:
      config.device = livekit::DeviceSelector::id(request.id);
      break;
  }

  switch (request.strategy) {
    case DeviceRequest::Strategy::Default:
      break; // default-constructed request accepts the device default format
    case DeviceRequest::Strategy::Exact:
    case DeviceRequest::Strategy::Closest: {
      // Validation guarantees all three components are present here.
      livekit::DeviceFormat format{
          request.resolution.value_or(livekit::CaptureResolution{}),
          request.framerate_fps.value_or(0),
          request.frame_format.value_or(livekit::DeviceFrameFormat::Nv12),
      };
      config.format = request.strategy == DeviceRequest::Strategy::Exact
                          ? livekit::DeviceFormatRequest::exact(format)
                          : livekit::DeviceFormatRequest::closest(format);
      break;
    }
    case DeviceRequest::Strategy::HighestFramerate: {
      livekit::DeviceFormatRequest::HighestFramerateConstraint constraint;
      constraint.resolution = request.resolution;
      constraint.frame_format = request.frame_format;
      config.format = livekit::DeviceFormatRequest::highestFramerate(constraint);
      break;
    }
    case DeviceRequest::Strategy::HighestResolution: {
      livekit::DeviceFormatRequest::HighestResolutionConstraint constraint;
      constraint.framerate_fps = request.framerate_fps;
      constraint.frame_format = request.frame_format;
      config.format = livekit::DeviceFormatRequest::highestResolution(constraint);
      break;
    }
  }

  return config;
}

std::future<std::shared_ptr<livekit::CaptureSource>> createCaptureSource(
    const ros_portal_config::CaptureSourceConfig& source) {
  switch (source.type) {
    case ros_portal_config::CaptureSourceType::Pattern:
      return livekit::CaptureSource::create(toPatternConfig(source));
    case ros_portal_config::CaptureSourceType::Device:
      return livekit::CaptureSource::create(toLiveKitConfig(toDeviceRequest(source)));
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
