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
      video_options:
        bitrate_kbps: 3500
        codec: "h264"
    - topic: "/odom"
      direction: "bidirectional"
      encoding: "jsonschema"
    - topic: "/teleop_cmd"
      direction: "in"
      preserve_id: true
)");

  EXPECT_EQ(config.topic_polling_period_ms, 500);
  EXPECT_EQ(config.ros_threads, 4);

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
  ASSERT_TRUE(config.topics[0].video_options.has_value());
  ASSERT_TRUE(config.topics[0].video_options->bitrate_kbps.has_value());
  EXPECT_EQ(*config.topics[0].video_options->bitrate_kbps, 3500);
  ASSERT_TRUE(config.topics[0].video_options->codec.has_value());
  EXPECT_EQ(*config.topics[0].video_options->codec, "h264");
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
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
      video_options:
        - bitrate_kbps
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

TEST(ConfigParserTest, RejectsInvalidVideoBitrate) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
      video_options:
        bitrate_kbps: -1
)",
      "expected positive integer");
}

TEST(ConfigParserTest, RejectsEmptyCodec) {
  expectInvalid(
      R"(
ros_portal:
  version: "0.0.1"
  topics:
    - topic: "/camera/image_raw"
      direction: "out"
      video_options:
        codec: ""
)",
      "expected nonempty string");
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
  EXPECT_EQ(config.ros_threads, 4);
  ASSERT_EQ(config.services.size(), 2u);
  ASSERT_EQ(config.topics.size(), 6u);
  EXPECT_EQ(config.topics[0].topic, "/camera/image_raw");
  EXPECT_FALSE(config.topics[0].preserve_id);
  EXPECT_FALSE(config.topics[0].max_rate_hz.has_value());
  EXPECT_EQ(config.topics[1].topic, "/lidar/points");
  ASSERT_TRUE(config.topics[1].max_rate_hz.has_value());
  EXPECT_DOUBLE_EQ(*config.topics[1].max_rate_hz, 10.0);
  EXPECT_EQ(config.topics[5].topic, "/teleop_cmd");
  EXPECT_TRUE(config.topics[5].preserve_id);
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
