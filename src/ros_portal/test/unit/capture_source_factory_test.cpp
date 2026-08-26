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

// Covers the config -> SDK translation and every per-type validation rule the
// config schema cannot express (it supports neither oneOf nor conditional
// subschemas).
//
// Deliberately not covered: createCaptureSource() itself, which reaches the
// FFI, and the livekit::DeviceVideoSourceConfig returned by toLiveKitConfig().
// livekit::DeviceSelector and livekit::DeviceFormatRequest keep their state
// private and befriend only livekit::CaptureSource, so nothing about a
// constructed value can be asserted. That is exactly why validation lands in
// the inspectable DeviceRequest, which is what these tests read.

#include "ros_portal/capture_source_factory.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ros_portal::capture {
namespace {

using ros_portal_config::CaptureSourceConfig;
using ros_portal_config::CaptureSourceType;

CaptureSourceConfig makeGstreamerSource() {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Gstreamer;
  source.pipeline = "videotestsrc ! x264enc name=lk_encoder ! appsink name=lk_appsink";
  return source;
}

CaptureSourceConfig makeDeviceSource() {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Device;
  source.device = ros_portal_config::DeviceSourceConfig{};
  return source;
}

CaptureSourceConfig makePatternSource() {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Pattern;
  return source;
}

ros_portal_config::DeviceFormatSelection makeFormat(ros_portal_config::DeviceFormatStrategy strategy) {
  ros_portal_config::DeviceFormatSelection format;
  format.strategy = strategy;
  return format;
}

/// Asserts the call throws std::invalid_argument mentioning @p expected_text.
template <typename Fn>
void expectRejected(Fn&& fn, const std::string& expected_text) {
  try {
    (void)fn();
    FAIL() << "Expected std::invalid_argument mentioning: " << expected_text;
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find(expected_text), std::string::npos) << error.what();
  }
}

// ---------------------------------------------------------------------------
// GStreamer
// ---------------------------------------------------------------------------

TEST(CaptureSourceFactoryTest, TranslatesGstreamerConfig) {
  auto source = makeGstreamerSource();
  source.codec = ros_portal_config::VideoCodec::H264;
  source.resolution = ros_portal_config::CaptureResolution{1280, 720};
  source.rate_control =
      ros_portal_config::GstreamerRateControl{"lk_encoder", "bitrate", ros_portal_config::GstreamerBitrateUnit::Kbps};

  const auto config = toGstreamerConfig(source);

  EXPECT_EQ(config.pipeline, *source.pipeline);
  ASSERT_TRUE(config.codec.has_value());
  EXPECT_EQ(*config.codec, livekit::VideoCodec::H264);
  ASSERT_TRUE(config.resolution.has_value());
  EXPECT_EQ(config.resolution->width, 1280);
  EXPECT_EQ(config.resolution->height, 720);
  ASSERT_TRUE(config.rate_control.has_value());
  EXPECT_EQ(config.rate_control->element, "lk_encoder");
  EXPECT_EQ(config.rate_control->property, "bitrate");
  EXPECT_EQ(config.rate_control->unit, livekit::GstreamerBitrateUnit::Kbps);
}

TEST(CaptureSourceFactoryTest, GstreamerBitrateUnitDefaultsToBps) {
  auto source = makeGstreamerSource();
  source.rate_control =
      ros_portal_config::GstreamerRateControl{"lk_encoder", "bitrate", ros_portal_config::GstreamerBitrateUnit::Bps};

  EXPECT_EQ(toGstreamerConfig(source).rate_control->unit, livekit::GstreamerBitrateUnit::Bps);
}

TEST(CaptureSourceFactoryTest, GstreamerRequiresPipeline) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Gstreamer;

  expectRejected([&] { return toGstreamerConfig(source); }, "requires a pipeline");
}

TEST(CaptureSourceFactoryTest, GstreamerRejectsDeviceBlock) {
  auto source = makeGstreamerSource();
  source.device = ros_portal_config::DeviceSourceConfig{};

  expectRejected([&] { return toGstreamerConfig(source); }, "'device' block");
}

TEST(CaptureSourceFactoryTest, GstreamerRejectsPatternBlock) {
  auto source = makeGstreamerSource();
  source.pattern = ros_portal_config::PatternSourceConfig{};

  expectRejected([&] { return toGstreamerConfig(source); }, "'pattern' block");
}

TEST(CaptureSourceFactoryTest, MapsEveryCodec) {
  const std::vector<std::pair<ros_portal_config::VideoCodec, livekit::VideoCodec>> expected{
      {ros_portal_config::VideoCodec::H264, livekit::VideoCodec::H264},
      {ros_portal_config::VideoCodec::H265, livekit::VideoCodec::H265},
      {ros_portal_config::VideoCodec::Vp8, livekit::VideoCodec::VP8},
      {ros_portal_config::VideoCodec::Vp9, livekit::VideoCodec::VP9},
      {ros_portal_config::VideoCodec::Av1, livekit::VideoCodec::AV1},
  };
  for (const auto& [configured, sdk] : expected) {
    EXPECT_EQ(toLiveKitCodec(configured), sdk);
  }
}

// ---------------------------------------------------------------------------
// Pattern
// ---------------------------------------------------------------------------

// Regression guard: the SDK rejects a zero resolution or frame rate, and the
// e2e pattern test only catches that when a live server is available.
TEST(CaptureSourceFactoryTest, PatternConfigDefaultsToNonZeroCharacteristics) {
  const auto config = toPatternConfig(makePatternSource());

  EXPECT_EQ(config.resolution.width, kDefaultPatternWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPatternHeight);
  EXPECT_EQ(config.framerate_fps, kDefaultPatternFramerateFps);
  EXPECT_GT(config.resolution.width, 0);
  EXPECT_GT(config.resolution.height, 0);
  EXPECT_GT(config.framerate_fps, 0u);
  EXPECT_EQ(config.pattern, livekit::Pattern::Gradient);
}

TEST(CaptureSourceFactoryTest, PatternConfigUsesConfiguredPattern) {
  auto source = makePatternSource();
  ros_portal_config::PatternSourceConfig pattern;
  pattern.pattern = ros_portal_config::VideoPattern::Logo;
  source.pattern = pattern;

  EXPECT_EQ(toPatternConfig(source).pattern, livekit::Pattern::Logo);
}

TEST(CaptureSourceFactoryTest, MapsEveryPattern) {
  const std::vector<std::pair<ros_portal_config::VideoPattern, livekit::Pattern>> expected{
      {ros_portal_config::VideoPattern::Gradient, livekit::Pattern::Gradient},
      {ros_portal_config::VideoPattern::Logo, livekit::Pattern::Logo},
  };
  for (const auto& [configured, sdk] : expected) {
    EXPECT_EQ(toLiveKitPattern(configured), sdk);
  }
}

TEST(CaptureSourceFactoryTest, PatternConfigUsesConfiguredCharacteristics) {
  auto source = makePatternSource();
  ros_portal_config::PatternSourceConfig pattern;
  pattern.resolution = ros_portal_config::CaptureResolution{1280, 720};
  pattern.framerate_fps = 15;
  source.pattern = pattern;

  const auto config = toPatternConfig(source);

  EXPECT_EQ(config.resolution.width, 1280);
  EXPECT_EQ(config.resolution.height, 720);
  EXPECT_EQ(config.framerate_fps, 15u);
}

TEST(CaptureSourceFactoryTest, PatternConfigFillsOnlyOmittedCharacteristics) {
  auto source = makePatternSource();
  ros_portal_config::PatternSourceConfig pattern;
  pattern.framerate_fps = 5;
  source.pattern = pattern;

  const auto config = toPatternConfig(source);

  EXPECT_EQ(config.framerate_fps, 5u);
  EXPECT_EQ(config.resolution.width, kDefaultPatternWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPatternHeight);
}

TEST(CaptureSourceFactoryTest, PatternRejectsGstreamerOptions) {
  auto source = makePatternSource();
  source.pipeline = "videotestsrc ! appsink name=lk_appsink";

  expectRejected([&] { return toPatternConfig(source); }, "does not accept gstreamer options");
}

TEST(CaptureSourceFactoryTest, PatternRejectionNamesTheOffendingFields) {
  auto source = makePatternSource();
  source.codec = ros_portal_config::VideoCodec::H264;
  source.rate_control = ros_portal_config::GstreamerRateControl{"e", "p", ros_portal_config::GstreamerBitrateUnit::Bps};

  expectRejected([&] { return toPatternConfig(source); }, "codec, rate_control");
}

TEST(CaptureSourceFactoryTest, PatternRejectsDeviceBlock) {
  auto source = makePatternSource();
  source.device = ros_portal_config::DeviceSourceConfig{};

  expectRejected([&] { return toPatternConfig(source); }, "'device' block");
}

// ---------------------------------------------------------------------------
// Device selection
// ---------------------------------------------------------------------------

TEST(CaptureSourceFactoryTest, DefaultsToPlatformDevice) {
  const auto request = toDeviceRequest(makeDeviceSource());

  EXPECT_EQ(request.selection, DeviceRequest::Selection::Default);
  EXPECT_EQ(request.strategy, DeviceRequest::Strategy::Default);
  EXPECT_FALSE(request.resolution.has_value());
  EXPECT_FALSE(request.framerate_fps.has_value());
  EXPECT_FALSE(request.frame_format.has_value());
}

TEST(CaptureSourceFactoryTest, TranslatesDeviceSelectionById) {
  auto source = makeDeviceSource();
  source.device->id = "0x8020000005ac8514";

  const auto request = toDeviceRequest(source);

  EXPECT_EQ(request.selection, DeviceRequest::Selection::Id);
  EXPECT_EQ(request.id, "0x8020000005ac8514");
}

TEST(CaptureSourceFactoryTest, TranslatesDeviceSelectionByIndex) {
  auto source = makeDeviceSource();
  source.device->index = 3;

  const auto request = toDeviceRequest(source);

  EXPECT_EQ(request.selection, DeviceRequest::Selection::Index);
  EXPECT_EQ(request.index, 3u);
}

TEST(CaptureSourceFactoryTest, RejectsBothDeviceIndexAndId) {
  auto source = makeDeviceSource();
  source.device->index = 0;
  source.device->id = "cam";

  expectRejected([&] { return toDeviceRequest(source); }, "at most one of 'index' or 'id'");
}

TEST(CaptureSourceFactoryTest, DeviceRequiresDeviceBlock) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Device;

  expectRejected([&] { return toDeviceRequest(source); }, "requires a 'device' block");
}

TEST(CaptureSourceFactoryTest, DeviceRejectsGstreamerOptions) {
  auto source = makeDeviceSource();
  source.pipeline = "v4l2src ! appsink name=lk_appsink";

  expectRejected([&] { return toDeviceRequest(source); }, "does not accept gstreamer options");
}

TEST(CaptureSourceFactoryTest, DeviceRejectsPatternBlock) {
  auto source = makeDeviceSource();
  source.pattern = ros_portal_config::PatternSourceConfig{};

  expectRejected([&] { return toDeviceRequest(source); }, "'pattern' block");
}

// ---------------------------------------------------------------------------
// Device format strategies
// ---------------------------------------------------------------------------

TEST(CaptureSourceFactoryTest, ExactAndClosestDefaultFrameFormatToNv12) {
  for (const auto strategy :
       {ros_portal_config::DeviceFormatStrategy::Exact, ros_portal_config::DeviceFormatStrategy::Closest}) {
    auto source = makeDeviceSource();
    auto format = makeFormat(strategy);
    format.resolution = ros_portal_config::CaptureResolution{1280, 720};
    format.framerate_fps = 30;
    source.device->format = format;

    const auto request = toDeviceRequest(source);

    EXPECT_EQ(request.strategy, strategy == ros_portal_config::DeviceFormatStrategy::Exact
                                    ? DeviceRequest::Strategy::Exact
                                    : DeviceRequest::Strategy::Closest);
    ASSERT_TRUE(request.resolution.has_value());
    EXPECT_EQ(request.resolution->width, 1280);
    EXPECT_EQ(request.resolution->height, 720);
    ASSERT_TRUE(request.framerate_fps.has_value());
    EXPECT_EQ(*request.framerate_fps, 30u);
    ASSERT_TRUE(request.frame_format.has_value()) << "exact/closest need a concrete format";
    EXPECT_EQ(*request.frame_format, livekit::DeviceFrameFormat::Nv12);
  }
}

TEST(CaptureSourceFactoryTest, ExactPassesThroughExplicitFrameFormat) {
  auto source = makeDeviceSource();
  auto format = makeFormat(ros_portal_config::DeviceFormatStrategy::Exact);
  format.resolution = ros_portal_config::CaptureResolution{640, 480};
  format.framerate_fps = 30;
  format.frame_format = ros_portal_config::DeviceFrameFormat::Mjpeg;
  source.device->format = format;

  const auto request = toDeviceRequest(source);

  ASSERT_TRUE(request.frame_format.has_value());
  EXPECT_EQ(*request.frame_format, livekit::DeviceFrameFormat::Mjpeg);
}

TEST(CaptureSourceFactoryTest, ExactAndClosestRequireResolutionAndFramerate) {
  for (const auto strategy :
       {ros_portal_config::DeviceFormatStrategy::Exact, ros_portal_config::DeviceFormatStrategy::Closest}) {
    auto missing_framerate = makeDeviceSource();
    auto format = makeFormat(strategy);
    format.resolution = ros_portal_config::CaptureResolution{1280, 720};
    missing_framerate.device->format = format;
    expectRejected([&] { return toDeviceRequest(missing_framerate); }, "requires both resolution and framerate_fps");

    auto missing_resolution = makeDeviceSource();
    auto other = makeFormat(strategy);
    other.framerate_fps = 30;
    missing_resolution.device->format = other;
    expectRejected([&] { return toDeviceRequest(missing_resolution); }, "requires both resolution and framerate_fps");
  }
}

// Locks in the deliberate absence of a frame-format default for the highest_*
// strategies: naming a format there selects it outright with no fallback, so
// defaulting to nv12 would silently exclude cameras whose top resolution is
// only offered as yuyv or mjpeg.
TEST(CaptureSourceFactoryTest, HighestStrategiesLeaveFrameFormatUnset) {
  for (const auto strategy : {ros_portal_config::DeviceFormatStrategy::HighestFramerate,
                              ros_portal_config::DeviceFormatStrategy::HighestResolution}) {
    auto source = makeDeviceSource();
    source.device->format = makeFormat(strategy);

    const auto request = toDeviceRequest(source);

    EXPECT_FALSE(request.frame_format.has_value());
  }
}

TEST(CaptureSourceFactoryTest, HighestFramerateAcceptsResolutionConstraint) {
  auto source = makeDeviceSource();
  auto format = makeFormat(ros_portal_config::DeviceFormatStrategy::HighestFramerate);
  format.resolution = ros_portal_config::CaptureResolution{640, 480};
  format.frame_format = ros_portal_config::DeviceFrameFormat::Yuyv;
  source.device->format = format;

  const auto request = toDeviceRequest(source);

  EXPECT_EQ(request.strategy, DeviceRequest::Strategy::HighestFramerate);
  ASSERT_TRUE(request.resolution.has_value());
  EXPECT_EQ(request.resolution->width, 640);
  EXPECT_FALSE(request.framerate_fps.has_value());
  ASSERT_TRUE(request.frame_format.has_value());
  EXPECT_EQ(*request.frame_format, livekit::DeviceFrameFormat::Yuyv);
}

TEST(CaptureSourceFactoryTest, HighestResolutionAcceptsFramerateConstraint) {
  auto source = makeDeviceSource();
  auto format = makeFormat(ros_portal_config::DeviceFormatStrategy::HighestResolution);
  format.framerate_fps = 15;
  source.device->format = format;

  const auto request = toDeviceRequest(source);

  EXPECT_EQ(request.strategy, DeviceRequest::Strategy::HighestResolution);
  ASSERT_TRUE(request.framerate_fps.has_value());
  EXPECT_EQ(*request.framerate_fps, 15u);
  EXPECT_FALSE(request.resolution.has_value());
}

TEST(CaptureSourceFactoryTest, HighestFramerateRejectsFramerate) {
  auto source = makeDeviceSource();
  auto format = makeFormat(ros_portal_config::DeviceFormatStrategy::HighestFramerate);
  format.framerate_fps = 30;
  source.device->format = format;

  expectRejected([&] { return toDeviceRequest(source); }, "does not accept framerate_fps");
}

TEST(CaptureSourceFactoryTest, HighestResolutionRejectsResolution) {
  auto source = makeDeviceSource();
  auto format = makeFormat(ros_portal_config::DeviceFormatStrategy::HighestResolution);
  format.resolution = ros_portal_config::CaptureResolution{1920, 1080};
  source.device->format = format;

  expectRejected([&] { return toDeviceRequest(source); }, "does not accept resolution");
}

// Catches enum-order skew between the schema and the SDK.
TEST(CaptureSourceFactoryTest, MapsEveryFrameFormat) {
  const std::vector<std::pair<ros_portal_config::DeviceFrameFormat, livekit::DeviceFrameFormat>> expected{
      {ros_portal_config::DeviceFrameFormat::I420, livekit::DeviceFrameFormat::I420},
      {ros_portal_config::DeviceFrameFormat::Nv12, livekit::DeviceFrameFormat::Nv12},
      {ros_portal_config::DeviceFrameFormat::Bgra, livekit::DeviceFrameFormat::Bgra},
      {ros_portal_config::DeviceFrameFormat::Rgb24, livekit::DeviceFrameFormat::Rgb24},
      {ros_portal_config::DeviceFrameFormat::Bgr24, livekit::DeviceFrameFormat::Bgr24},
      {ros_portal_config::DeviceFrameFormat::Yuyv, livekit::DeviceFrameFormat::Yuyv},
      {ros_portal_config::DeviceFrameFormat::Uyvy, livekit::DeviceFrameFormat::Uyvy},
      {ros_portal_config::DeviceFrameFormat::Grey, livekit::DeviceFrameFormat::Grey},
      {ros_portal_config::DeviceFrameFormat::Mjpeg, livekit::DeviceFrameFormat::Mjpeg},
  };
  for (const auto& [configured, sdk] : expected) {
    EXPECT_EQ(toLiveKitFrameFormat(configured), sdk);
  }
}

// ---------------------------------------------------------------------------
// Publish options
// ---------------------------------------------------------------------------

TEST(CaptureSourceFactoryTest, PublishOptionsAlwaysMarkCameraSource) {
  const auto options = capturePublishOptions(std::nullopt);

  ASSERT_TRUE(options.source.has_value());
  EXPECT_EQ(*options.source, livekit::TrackSource::SOURCE_CAMERA);
  EXPECT_FALSE(options.video_encoding.has_value());
}

TEST(CaptureSourceFactoryTest, PublishOptionsDisableSimulcastByDefault) {
  const auto options = capturePublishOptions(std::nullopt);

  ASSERT_TRUE(options.simulcast.has_value());
  EXPECT_FALSE(*options.simulcast);
}

TEST(CaptureSourceFactoryTest, PublishOptionsCarrySimulcastWithoutOverrides) {
  const auto options = capturePublishOptions(std::nullopt, true);

  ASSERT_TRUE(options.simulcast.has_value());
  EXPECT_TRUE(*options.simulcast);
  EXPECT_FALSE(options.video_encoding.has_value());
}

TEST(CaptureSourceFactoryTest, PublishOptionsCarrySimulcastAlongsideOverrides) {
  ros_portal_config::VideoPublishOptions configured;
  configured.max_bitrate_bps = 2000000;

  const auto options = capturePublishOptions(configured, true);

  ASSERT_TRUE(options.simulcast.has_value());
  EXPECT_TRUE(*options.simulcast);
  ASSERT_TRUE(options.video_encoding.has_value());
  EXPECT_EQ(options.video_encoding->max_bitrate, 2000000u);
}

TEST(CaptureSourceFactoryTest, PublishOptionsCarryBitrateOnly) {
  ros_portal_config::VideoPublishOptions configured;
  configured.max_bitrate_bps = 3500000;

  const auto options = capturePublishOptions(configured);

  ASSERT_TRUE(options.video_encoding.has_value());
  EXPECT_EQ(options.video_encoding->max_bitrate, 3500000u);
  EXPECT_DOUBLE_EQ(options.video_encoding->max_framerate, 0.0);
}

TEST(CaptureSourceFactoryTest, PublishOptionsCarryFramerateOnly) {
  ros_portal_config::VideoPublishOptions configured;
  configured.max_framerate = 30;

  const auto options = capturePublishOptions(configured);

  ASSERT_TRUE(options.video_encoding.has_value());
  EXPECT_DOUBLE_EQ(options.video_encoding->max_framerate, 30.0);
  EXPECT_EQ(options.video_encoding->max_bitrate, 0u);
}

TEST(CaptureSourceFactoryTest, PublishOptionsCarryBothOverrides) {
  ros_portal_config::VideoPublishOptions configured;
  configured.max_bitrate_bps = 1000000;
  configured.max_framerate = 15;

  const auto options = capturePublishOptions(configured);

  ASSERT_TRUE(options.video_encoding.has_value());
  EXPECT_EQ(options.video_encoding->max_bitrate, 1000000u);
  EXPECT_DOUBLE_EQ(options.video_encoding->max_framerate, 15.0);
}

TEST(CaptureSourceFactoryTest, PublishOptionsOmitEncodingWhenEmpty) {
  const auto options = capturePublishOptions(ros_portal_config::VideoPublishOptions{});

  EXPECT_FALSE(options.video_encoding.has_value());
}

} // namespace
} // namespace ros_portal::capture
