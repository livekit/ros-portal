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

#include "ros_portal_config/config/config_parser.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace ros_portal_config {
namespace {

RosPortalConfig parse(const std::string& yaml) { return ConfigParser{}.parseString(yaml); }

void expectInvalid(const std::string& yaml, const std::string& expected_text) {
  try {
    (void)parse(yaml);
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_NE(std::string(e.what()).find(expected_text), std::string::npos) << e.what();
  }
}

TEST(ConfigParserTest, ParsesMinimalConfig) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
)");

  EXPECT_EQ(config.version, "0.0.1");
  EXPECT_EQ(config.topic_polling_period_ms, 500);
  EXPECT_EQ(config.ros_threads, 0);
  EXPECT_TRUE(config.services.empty());
  EXPECT_TRUE(config.topics.empty());
  EXPECT_TRUE(config.video_sources.empty());
}

TEST(ConfigParserTest, IgnoresOptionalSchemaKey) {
  const auto config = parse(R"(
# Copyright 2026 LiveKit
$schema: https://raw.githubusercontent.com/livekit/ros-portal/main/src/ros_portal_config/schema/ros_portal_config.schema.json
ros_portal:
  version: "0.0.1"
)");

  EXPECT_EQ(config.version, "0.0.1");
}

TEST(ConfigParserTest, ParsesFullConfig) {
  const auto config = parse(
      R"(
ros_portal:
  version: "0.0.1"
  topic_polling_period_ms: 500
  ros_threads: 4
  services:
    - service: "/go_to_pose"
      direction: "out"
      participant: "robot-a"
      msg_type: "nav2_msgs/srv/ComputePathToPose"
    - service: "/provide_status"
      direction: "out"
      participant: "robot-a"
      msg_type: "std_srvs/srv/Trigger"
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
      max_rate_hz: 15
    - topic: "/odom"
      direction: "bidirectional"
      encoding: "jsonschema"
    - topic: "/teleop_cmd"
      direction: "in"
      preserve_id: true
  video_sources:
    - track_name: "front_camera"
      source:
        type: "gstreamer"
        pipeline: "videotestsrc ! x264enc name=lk_encoder ! appsink name=lk_appsink"
        codec: "h264"
        resolution:
          width: 1920
          height: 1080
        rate_control:
          element: "lk_encoder"
          property: "bitrate"
          unit: "kbps"
      publish_options:
        max_bitrate_bps: 3500000
        max_framerate: 30
    - track_name: "demo_camera"
      simulcast: true
      source:
        type: "demo"
    - track_name: "usb_camera"
      source:
        type: "device"
        device:
          id: "0x8020000005ac8514"
          format:
            strategy: "closest"
            resolution:
              width: 1280
              height: 720
            framerate_fps: 30
            frame_format: "nv12"
)");

  EXPECT_EQ(config.topic_polling_period_ms, 500);
  EXPECT_EQ(config.ros_threads, 0); // schema default; neither fixture sets ros_threads

  ASSERT_EQ(config.services.size(), 2u);
  EXPECT_EQ(config.services[0].service, "/go_to_pose");
  EXPECT_EQ(config.services[0].direction, Direction::Out);
  EXPECT_EQ(config.services[0].participant, "robot-a");
  EXPECT_EQ(config.services[0].msg_type, "nav2_msgs/srv/ComputePathToPose");
  EXPECT_EQ(config.services[1].direction, Direction::Out);
  EXPECT_EQ(config.services[1].msg_type, "std_srvs/srv/Trigger");

  ASSERT_EQ(config.topics.size(), 3u);
  EXPECT_EQ(config.topics[0].topic, "/camera/image_raw");
  EXPECT_EQ(config.topics[0].direction, Direction::Out);
  EXPECT_FALSE(config.topics[0].preserve_id);
  ASSERT_TRUE(config.topics[0].max_rate_hz.has_value());
  EXPECT_DOUBLE_EQ(*config.topics[0].max_rate_hz, 15.0);
  EXPECT_EQ(config.topics[0].encoding, Encoding::Ros2msg); // default
  EXPECT_EQ(config.topics[1].direction, Direction::Bidirectional);
  EXPECT_FALSE(config.topics[1].preserve_id);
  EXPECT_FALSE(config.topics[1].max_rate_hz.has_value());
  EXPECT_EQ(config.topics[1].encoding, Encoding::Jsonschema);
  EXPECT_EQ(config.topics[2].direction, Direction::In);
  EXPECT_TRUE(config.topics[2].preserve_id);
  EXPECT_EQ(config.topics[2].encoding, Encoding::Ros2msg); // default

  ASSERT_EQ(config.video_sources.size(), 3u);
  EXPECT_EQ(config.video_sources[0].track_name, "front_camera");
  EXPECT_FALSE(config.video_sources[0].simulcast); // default
  EXPECT_EQ(config.video_sources[0].source.type, CaptureSourceType::Gstreamer);
  ASSERT_TRUE(config.video_sources[0].source.pipeline.has_value());
  EXPECT_EQ(*config.video_sources[0].source.pipeline,
            "videotestsrc ! x264enc name=lk_encoder ! appsink name=lk_appsink");
  ASSERT_TRUE(config.video_sources[0].source.codec.has_value());
  EXPECT_EQ(*config.video_sources[0].source.codec, VideoCodec::H264);
  ASSERT_TRUE(config.video_sources[0].source.resolution.has_value());
  EXPECT_EQ(config.video_sources[0].source.resolution->width, 1920);
  EXPECT_EQ(config.video_sources[0].source.resolution->height, 1080);
  ASSERT_TRUE(config.video_sources[0].source.rate_control.has_value());
  EXPECT_EQ(config.video_sources[0].source.rate_control->element, "lk_encoder");
  EXPECT_EQ(config.video_sources[0].source.rate_control->property, "bitrate");
  EXPECT_EQ(config.video_sources[0].source.rate_control->unit, GstreamerBitrateUnit::Kbps);
  ASSERT_TRUE(config.video_sources[0].publish_options.has_value());
  ASSERT_TRUE(config.video_sources[0].publish_options->max_bitrate_bps.has_value());
  EXPECT_EQ(*config.video_sources[0].publish_options->max_bitrate_bps, 3500000);
  ASSERT_TRUE(config.video_sources[0].publish_options->max_framerate.has_value());
  EXPECT_EQ(*config.video_sources[0].publish_options->max_framerate, 30);
  EXPECT_EQ(config.video_sources[1].track_name, "demo_camera");
  EXPECT_TRUE(config.video_sources[1].simulcast);
  EXPECT_EQ(config.video_sources[1].source.type, CaptureSourceType::Demo);
  EXPECT_FALSE(config.video_sources[1].source.pipeline.has_value());
  EXPECT_FALSE(config.video_sources[1].source.demo.has_value());

  EXPECT_EQ(config.video_sources[2].track_name, "usb_camera");
  EXPECT_EQ(config.video_sources[2].source.type, CaptureSourceType::Device);
  EXPECT_FALSE(config.video_sources[2].source.pipeline.has_value());
  ASSERT_TRUE(config.video_sources[2].source.device.has_value());
  const auto& device = *config.video_sources[2].source.device;
  ASSERT_TRUE(device.id.has_value());
  EXPECT_EQ(*device.id, "0x8020000005ac8514");
  EXPECT_FALSE(device.index.has_value());
  ASSERT_TRUE(device.format.has_value());
  EXPECT_EQ(device.format->strategy, DeviceFormatStrategy::Closest);
  ASSERT_TRUE(device.format->resolution.has_value());
  EXPECT_EQ(device.format->resolution->width, 1280);
  EXPECT_EQ(device.format->resolution->height, 720);
  ASSERT_TRUE(device.format->framerate_fps.has_value());
  EXPECT_EQ(*device.format->framerate_fps, 30);
  ASSERT_TRUE(device.format->frame_format.has_value());
  EXPECT_EQ(*device.format->frame_format, DeviceFrameFormat::Nv12);
}

TEST(ConfigParserTest, ParsesDeviceSourceWithDefaults) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "default_camera"
      source:
        type: "device"
        device: {}
)");

  ASSERT_EQ(config.video_sources.size(), 1u);
  EXPECT_EQ(config.video_sources[0].source.type, CaptureSourceType::Device);
  ASSERT_TRUE(config.video_sources[0].source.device.has_value());
  const auto& device = *config.video_sources[0].source.device;
  EXPECT_FALSE(device.index.has_value());
  EXPECT_FALSE(device.id.has_value());
  EXPECT_FALSE(device.format.has_value());
}

TEST(ConfigParserTest, ParsesDeviceSourceByIndex) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "indexed_camera"
      source:
        type: "device"
        device:
          index: 2
)");

  ASSERT_TRUE(config.video_sources[0].source.device.has_value());
  const auto& device = *config.video_sources[0].source.device;
  ASSERT_TRUE(device.index.has_value());
  EXPECT_EQ(*device.index, 2);
  EXPECT_FALSE(device.id.has_value());
}

// Guards the deliberate absence of a schema-level frame_format default: naming a
// format for the highest_* strategies selects it outright with no fallback.
TEST(ConfigParserTest, ParsesHighestResolutionStrategyWithoutFrameFormat) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "max_res_camera"
      source:
        type: "device"
        device:
          format:
            strategy: "highest_resolution"
            framerate_fps: 15
)");

  const auto& format = *config.video_sources[0].source.device->format;
  EXPECT_EQ(format.strategy, DeviceFormatStrategy::HighestResolution);
  EXPECT_FALSE(format.frame_format.has_value());
  EXPECT_FALSE(format.resolution.has_value());
  ASSERT_TRUE(format.framerate_fps.has_value());
  EXPECT_EQ(*format.framerate_fps, 15);
}

TEST(ConfigParserTest, ParsesHighestFramerateStrategy) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "max_fps_camera"
      source:
        type: "device"
        device:
          format:
            strategy: "highest_framerate"
            resolution:
              width: 640
              height: 480
            frame_format: "mjpeg"
)");

  const auto& format = *config.video_sources[0].source.device->format;
  EXPECT_EQ(format.strategy, DeviceFormatStrategy::HighestFramerate);
  ASSERT_TRUE(format.resolution.has_value());
  EXPECT_EQ(format.resolution->width, 640);
  ASSERT_TRUE(format.frame_format.has_value());
  EXPECT_EQ(*format.frame_format, DeviceFrameFormat::Mjpeg);
  EXPECT_FALSE(format.framerate_fps.has_value());
}

TEST(ConfigParserTest, ParsesDemoSourceCharacteristics) {
  const auto config = parse(R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "demo_camera"
      source:
        type: "demo"
        demo:
          resolution:
            width: 1280
            height: 720
          framerate_fps: 15
)");

  ASSERT_TRUE(config.video_sources[0].source.demo.has_value());
  const auto& demo = *config.video_sources[0].source.demo;
  ASSERT_TRUE(demo.resolution.has_value());
  EXPECT_EQ(demo.resolution->width, 1280);
  EXPECT_EQ(demo.resolution->height, 720);
  ASSERT_TRUE(demo.framerate_fps.has_value());
  EXPECT_EQ(*demo.framerate_fps, 15);
}

TEST(ConfigParserTest, RejectsNonBooleanSimulcast) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      simulcast: "yes please"
      source:
        type: "demo"
)",
      "boolean");
}

TEST(ConfigParserTest, RejectsUnknownDeviceField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          devise_id: "typo"
)",
      "unknown field 'devise_id'");
}

TEST(ConfigParserTest, RejectsUnknownDeviceFormatField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          format:
            strategy: "closest"
            fps: 30
)",
      "unknown field 'fps'");
}

TEST(ConfigParserTest, RejectsDeviceFormatWithoutStrategy) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          format:
            framerate_fps: 30
)",
      "strategy");
}

TEST(ConfigParserTest, RejectsInvalidDeviceFormatStrategy) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          format:
            strategy: "best"
)",
      "'exact'");
}

TEST(ConfigParserTest, RejectsInvalidDeviceFrameFormat) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          format:
            strategy: "closest"
            frame_format: "yuv420"
)",
      "'i420'");
}

TEST(ConfigParserTest, RejectsZeroDeviceFramerate) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          format:
            strategy: "closest"
            framerate_fps: 0
)",
      "expected positive integer");
}

TEST(ConfigParserTest, RejectsEmptyDeviceId) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "camera"
      source:
        type: "device"
        device:
          id: ""
)",
      "expected nonempty string");
}

TEST(ConfigParserTest, ParsesRos2IdlEncoding) {
  const auto config = parse(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/state"
      direction: "out"
      encoding: "ros2idl"
)");

  ASSERT_EQ(config.topics.size(), 1u);
  EXPECT_EQ(config.topics[0].encoding, Encoding::Ros2idl);
}

TEST(ConfigParserTest, RejectsInvalidTopicEncoding) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/state"
      direction: "out"
      encoding: "protobuf"
)",
      "expected 'ros2msg', 'ros2idl', or 'jsonschema'");
}

TEST(ConfigParserTest, RejectsNonNumericMaxRateHz) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/tf"
      direction: "out"
      max_rate_hz: "fast"
)",
      "expected number");
}

TEST(ConfigParserTest, RejectsUnknownRootField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
extra: true
)",
      "unknown field 'extra'");
}

TEST(ConfigParserTest, RejectsMisspelledParticipantField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/provide_status"
      direction: "out"
      msg_type: "std_srvs/srv/Trigger"
      particpant: "robot-a"
)",
      "unknown field 'particpant'");
}

TEST(ConfigParserTest, RejectsUnsupportedVersion) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.2"
)",
      "expected '0.0.1'");
}

TEST(ConfigParserTest, RejectsWrongScalarType) {
  expectInvalid(
      R"(
ros_portal:
  version:
    major: 0
)",
      "expected string");
}

TEST(ConfigParserTest, RejectsWrongSequenceType) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    topic: "/odom"
    direction: "out"
)",
      "expected sequence");
}

TEST(ConfigParserTest, RejectsWrongMapType) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "front_camera"
      source:
        - type
)",
      "expected map");
}

TEST(ConfigParserTest, RejectsInvalidServiceDirection) {
  // Services only support "out"; "in" and "bidirectional" are not accepted.
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "in"
      participant: "robot-a"
      msg_type: "nav2_msgs/srv/ComputePathToPose"
)",
      "expected 'out'");
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "bidirectional"
      participant: "robot-a"
      msg_type: "nav2_msgs/srv/ComputePathToPose"
)",
      "expected 'out'");
}

TEST(ConfigParserTest, RejectsInvalidTopicDirection) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/teleop_cmd"
      direction: "sideways"
)",
      "expected 'in', 'out', or 'bidirectional'");
}

TEST(ConfigParserTest, RejectsMissingServiceParticipant) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "out"
      msg_type: "nav2_msgs/srv/ComputePathToPose"
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsMissingServiceMsgType) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "out"
      participant: "robot-a"
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsEmptyServiceMsgType) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "out"
      participant: "robot-a"
      msg_type: ""
)",
      "expected nonempty string");
}

TEST(ConfigParserTest, RejectsMissingTopicName) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - direction: "out"
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsEmptyTopicName) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: ""
      direction: "out"
)",
      "expected nonempty string");
}

TEST(ConfigParserTest, RejectsRemovedTopicVideoOptions) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
      video_options:
        bitrate_kbps: 3500
)",
      "unknown field 'video_options'");
}

TEST(ConfigParserTest, RejectsInvalidVideoSourceCodec) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "front_camera"
      source:
        type: "gstreamer"
        pipeline: "videotestsrc ! x264enc"
        codec: "mpeg2"
)",
      "expected 'h264', 'h265', 'vp8', 'vp9', or 'av1'");
}

TEST(ConfigParserTest, RejectsInvalidVideoSourcePublishBitrate) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  video_sources:
    - track_name: "front_camera"
      source:
        type: "gstreamer"
        pipeline: "videotestsrc ! x264enc"
      publish_options:
        max_bitrate_bps: 0
)",
      "expected positive integer");
}

TEST(ConfigParserTest, RejectsAudioOptions) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/mic/audio_left"
      direction: "out"
      audio_options:
        bitrate_kbps: 500
)",
      "unknown field 'audio_options'");
}

TEST(ConfigParserTest, ParsesFile) {
  const auto path = std::filesystem::path(ROS_PORTAL_CONFIG_TEST_DIR) / "config" / "test_config.yaml";

  const auto config = ConfigParser{}.parseFile(path);

  EXPECT_EQ(config.version, "0.0.1");
  EXPECT_EQ(config.topic_polling_period_ms, 500);
  EXPECT_EQ(config.ros_threads, 0); // schema default; neither fixture sets ros_threads
  ASSERT_EQ(config.services.size(), 2u);
  ASSERT_EQ(config.topics.size(), 6u);
  ASSERT_EQ(config.video_sources.size(), 3u);
  EXPECT_EQ(config.topics[0].topic, "/camera/image_raw");
  EXPECT_FALSE(config.topics[0].preserve_id);
  EXPECT_FALSE(config.topics[0].max_rate_hz.has_value());
  EXPECT_EQ(config.topics[1].topic, "/lidar/points");
  ASSERT_TRUE(config.topics[1].max_rate_hz.has_value());
  EXPECT_DOUBLE_EQ(*config.topics[1].max_rate_hz, 10.0);
  EXPECT_EQ(config.topics[5].topic, "/teleop_cmd");
  EXPECT_TRUE(config.topics[5].preserve_id);
  EXPECT_EQ(config.video_sources[0].track_name, "front_camera");
  EXPECT_FALSE(config.video_sources[0].simulcast);
  EXPECT_TRUE(config.video_sources[1].simulcast);
  EXPECT_EQ(config.video_sources[1].source.type, CaptureSourceType::Demo);
  ASSERT_TRUE(config.video_sources[1].source.demo.has_value());
  EXPECT_EQ(config.video_sources[1].source.demo->resolution->width, 640);
  EXPECT_EQ(config.video_sources[2].source.type, CaptureSourceType::Device);
  ASSERT_TRUE(config.video_sources[2].source.device.has_value());
  EXPECT_EQ(*config.video_sources[2].source.device->index, 0);
}

TEST(ConfigParserTest, ParsesEmptySequences) {
  const auto config = parse(
      R"(
ros_portal:
  version: "0.0.1"
  services: []
  topics: []
)");

  EXPECT_TRUE(config.services.empty());
  EXPECT_TRUE(config.topics.empty());
}

TEST(ConfigParserTest, ConfigErrorExposesStructuredFields) {
  try {
    (void)parse(R"(
ros_portal:
  version: "0.0.2"
)");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "$.ros_portal.version at line 3, column 12");
    EXPECT_EQ(e.expected(), "'0.0.1'");
    EXPECT_EQ(e.detail(), "found '0.0.2'");
  }
}

TEST(ConfigParserTest, ErrorContextIncludesLineAndColumn) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/teleop_cmd"
      direction: "sideways"
)",
      "at line 6, column 18");
}

TEST(ConfigParserTest, MissingFieldErrorHasNoLineColumn) {
  try {
    (void)parse(
        R"(
ros_portal:
  version: "0.0.1"
  topics:
    - direction: "out"
)");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "$.ros_portal.topics[0].topic");
    EXPECT_EQ(e.detail(), "missing required field");
    EXPECT_EQ(std::string(e.what()).find("at line"), std::string::npos) << e.what();
  }
}

TEST(ConfigParserTest, RejectsMalformedYamlString) {
  try {
    (void)parse("ros_portal: \"unterminated");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "<string>");
    EXPECT_EQ(e.expected(), "valid YAML config");
  }
}

TEST(ConfigParserTest, RejectsEmptyDocument) { expectInvalid("", "expected map"); }

TEST(ConfigParserTest, RejectsNonMapRoot) { expectInvalid("- just a sequence", "expected map"); }

TEST(ConfigParserTest, RejectsMissingRosPortalKey) {
  expectInvalid(
      R"(
unrelated: true
)",
      "unknown field 'unrelated'");
}

TEST(ConfigParserTest, RejectsMissingVersion) {
  expectInvalid(
      R"(
ros_portal: {}
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsUnknownRoomNameField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  room_name: "robo_room"
)",
      "unknown field 'room_name'");
}

TEST(ConfigParserTest, RejectsRoomOptionsField) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  room_options:
    join_retries: 3
)",
      "unknown field 'room_options'");
}

TEST(ConfigParserTest, RejectsMissingServiceDirection) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      participant: "robot-a"
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsMissingTopicDirection) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/odom"
)",
      "missing required field");
}

TEST(ConfigParserTest, RejectsEmptyServiceName) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: ""
      direction: "out"
      participant: "robot-a"
)",
      "expected nonempty string");
}

TEST(ConfigParserTest, RejectsEmptyParticipant) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  services:
    - service: "/go_to_pose"
      direction: "out"
      participant: ""
)",
      "expected nonempty string");
}

TEST(ConfigParserTest, RejectsInvalidTopicPollingPeriod) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topic_polling_period_ms: 0
)",
      "expected positive integer");
}

TEST(ConfigParserTest, RejectsInvalidRosThreads) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  ros_threads: -1
)",
      "expected integer >= 0");
}

TEST(ConfigParserTest, RejectsNonScalarMapKey) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  ? [complex, key]
  : true
)",
      "expected string key");
}

TEST(ConfigParserTest, ParsesFromMissingFileThrowsConfigError) {
  const auto path = std::filesystem::path(ROS_PORTAL_CONFIG_TEST_DIR) / "config" / "does_not_exist.yaml";

  try {
    (void)ConfigParser{}.parseFile(path);
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), path.string());
    EXPECT_EQ(e.expected(), "valid YAML config");
  }
}

TEST(ConfigParserTest, ParsesMalformedFileThrowsConfigError) {
  const auto unique = std::filesystem::file_time_type::clock::now().time_since_epoch().count();
  const auto path =
      std::filesystem::temp_directory_path() / ("ros_portal_malformed_config_" + std::to_string(unique) + ".yaml");
  std::ofstream out(path);
  out << "ros_portal: \"unterminated";
  out.close();

  try {
    (void)ConfigParser{}.parseFile(path);
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), path.string());
    EXPECT_EQ(e.expected(), "valid YAML config");
  }
  std::remove(path.string().c_str());
}

TEST(ConfigParserTest, ConvertsDirectionToString) {
  EXPECT_STREQ(toString(Direction::In), "in");
  EXPECT_STREQ(toString(Direction::Out), "out");
  EXPECT_STREQ(toString(Direction::Bidirectional), "bidirectional");
}

} // namespace
} // namespace ros_portal_config
