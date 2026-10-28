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
// FFI.

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

CaptureSourceConfig makePatternSource() {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Pattern;
  return source;
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
  EXPECT_EQ(config.rate_control->unit, livekit::capture::GstreamerBitrateUnit::Kbps);
}

TEST(CaptureSourceFactoryTest, GstreamerBitrateUnitDefaultsToBps) {
  auto source = makeGstreamerSource();
  source.rate_control =
      ros_portal_config::GstreamerRateControl{"lk_encoder", "bitrate", ros_portal_config::GstreamerBitrateUnit::Bps};

  EXPECT_EQ(toGstreamerConfig(source).rate_control->unit, livekit::capture::GstreamerBitrateUnit::Bps);
}

TEST(CaptureSourceFactoryTest, RejectsNonPositiveDimensionsBeforeUnsignedConversion) {
  for (const auto resolution :
       {ros_portal_config::CaptureResolution{-1, 480}, ros_portal_config::CaptureResolution{640, 0}}) {
    auto gstreamer = makeGstreamerSource();
    gstreamer.resolution = resolution;
    EXPECT_THROW((void)toGstreamerConfig(gstreamer), std::invalid_argument);

    auto pattern = makePatternSource();
    pattern.pattern = ros_portal_config::PatternSourceConfig{};
    pattern.pattern->resolution = resolution;
    EXPECT_THROW((void)toPatternConfig(pattern), std::invalid_argument);

    CaptureSourceConfig clock;
    clock.type = CaptureSourceType::Clock;
    clock.clock = ros_portal_config::ClockSourceConfig{};
    clock.clock->resolution = resolution;
    EXPECT_THROW((void)toClockConfig(clock), std::invalid_argument);
  }
}

TEST(CaptureSourceFactoryTest, RejectsNonPositiveFrameRatesBeforeUnsignedConversion) {
  for (const auto framerate : {-1, 0}) {
    auto pattern = makePatternSource();
    pattern.pattern = ros_portal_config::PatternSourceConfig{};
    pattern.pattern->framerate_fps = framerate;
    EXPECT_THROW((void)toPatternConfig(pattern), std::invalid_argument);

    CaptureSourceConfig clock;
    clock.type = CaptureSourceType::Clock;
    clock.clock = ros_portal_config::ClockSourceConfig{};
    clock.clock->framerate_fps = framerate;
    EXPECT_THROW((void)toClockConfig(clock), std::invalid_argument);
  }
}

TEST(CaptureSourceFactoryTest, GstreamerRequiresPipeline) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Gstreamer;

  expectRejected([&] { return toGstreamerConfig(source); }, "requires a pipeline");
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

  EXPECT_EQ(config.resolution.width, kDefaultPixelWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPixelHeight);
  EXPECT_EQ(config.framerate_fps, kDefaultPixelFramerateFps);
  EXPECT_GT(config.resolution.width, 0);
  EXPECT_GT(config.resolution.height, 0);
  EXPECT_GT(config.framerate_fps, 0u);
  EXPECT_EQ(config.pattern, livekit::capture::Pattern::Gradient);
}

TEST(CaptureSourceFactoryTest, PatternConfigUsesConfiguredPattern) {
  auto source = makePatternSource();
  ros_portal_config::PatternSourceConfig pattern;
  pattern.pattern = ros_portal_config::VideoPattern::Logo;
  source.pattern = pattern;

  EXPECT_EQ(toPatternConfig(source).pattern, livekit::capture::Pattern::Logo);
}

TEST(CaptureSourceFactoryTest, MapsEveryPattern) {
  const std::vector<std::pair<ros_portal_config::VideoPattern, livekit::capture::Pattern>> expected{
      {ros_portal_config::VideoPattern::Gradient, livekit::capture::Pattern::Gradient},
      {ros_portal_config::VideoPattern::Logo, livekit::capture::Pattern::Logo},
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
  EXPECT_EQ(config.resolution.width, kDefaultPixelWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPixelHeight);
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

TEST(CaptureSourceFactoryTest, ClockConfigDefaultsToNonZeroCharacteristics) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Clock;
  const auto config = toClockConfig(source);

  EXPECT_EQ(config.resolution.width, kDefaultPixelWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPixelHeight);
  EXPECT_EQ(config.framerate_fps, kDefaultPixelFramerateFps);
}

TEST(CaptureSourceFactoryTest, ClockConfigUsesConfiguredCharacteristics) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Clock;
  source.clock = ros_portal_config::ClockSourceConfig{};
  source.clock->resolution = ros_portal_config::CaptureResolution{1280, 720};
  source.clock->framerate_fps = 15;
  const auto config = toClockConfig(source);

  EXPECT_EQ(config.resolution.width, 1280);
  EXPECT_EQ(config.resolution.height, 720);
  EXPECT_EQ(config.framerate_fps, 15u);
}

TEST(CaptureSourceFactoryTest, ClockConfigFillsOnlyOmittedCharacteristics) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Clock;
  source.clock = ros_portal_config::ClockSourceConfig{};
  source.clock->framerate_fps = 5;
  const auto config = toClockConfig(source);

  EXPECT_EQ(config.resolution.width, kDefaultPixelWidth);
  EXPECT_EQ(config.resolution.height, kDefaultPixelHeight);
  EXPECT_EQ(config.framerate_fps, 5u);
}

TEST(CaptureSourceFactoryTest, ClockRejectsGstreamerOptions) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Clock;
  source.pipeline = "videotestsrc ! vp8enc";
  source.codec = ros_portal_config::VideoCodec::Vp8;
  source.resolution = ros_portal_config::CaptureResolution{640, 480};
  source.rate_control = ros_portal_config::GstreamerRateControl{"e", "p", ros_portal_config::GstreamerBitrateUnit::Bps};

  expectRejected([&] { return toClockConfig(source); }, "pipeline, codec, resolution, rate_control");
}

TEST(CaptureSourceFactoryTest, ClockRejectsPatternBlock) {
  CaptureSourceConfig source;
  source.type = CaptureSourceType::Clock;
  source.pattern = ros_portal_config::PatternSourceConfig{};

  expectRejected([&] { return toClockConfig(source); }, "'pattern' block");
}

TEST(CaptureSourceFactoryTest, OtherSourcesRejectClockBlock) {
  auto gstreamer = makeGstreamerSource();
  gstreamer.clock = ros_portal_config::ClockSourceConfig{};
  expectRejected([&] { return toGstreamerConfig(gstreamer); }, "'clock' block");

  auto pattern = makePatternSource();
  pattern.clock = ros_portal_config::ClockSourceConfig{};
  expectRejected([&] { return toPatternConfig(pattern); }, "'clock' block");
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
