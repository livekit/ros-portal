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

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <set>
#include <string>

#include "config/utils.hpp"
#include "ros2_livekit_bridge_config/config/error.hpp"

namespace ros2_livekit_bridge_config::utils {
namespace {

YAML::Node node(const std::string& yaml) { return YAML::Load(yaml); }

TEST(ConfigUtilsTest, FieldPathJoinsWithDot) {
  EXPECT_EQ(fieldPath("$", "version"), "$.version");
  EXPECT_EQ(fieldPath("$.ros2_livekit_bridge", "topics"), "$.ros2_livekit_bridge.topics");
  EXPECT_EQ(fieldPath("", "x"), ".x");
}

TEST(ConfigUtilsTest, NodeContextAppendsLineAndColumn) {
  EXPECT_EQ(nodeContext("$", node("value")), "$ at line 1, column 1");
}

TEST(ConfigUtilsTest, NodeContextWithoutMarkReturnsPath) {
  YAML::Node undefined;
  EXPECT_EQ(nodeContext("$.topics", undefined), "$.topics");
}

TEST(ConfigUtilsTest, FailThrowsConfigErrorWithLocation) {
  try {
    fail("$.version", node("0.0.2"), "'0.0.1'", "found '0.0.2'");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "$.version at line 1, column 1");
    EXPECT_EQ(e.expected(), "'0.0.1'");
    EXPECT_EQ(e.detail(), "found '0.0.2'");
  }
}

TEST(ConfigUtilsTest, FailMissingMarksFieldMissing) {
  try {
    failMissing("$.version", "string");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "$.version");
    EXPECT_EQ(e.detail(), "missing required field");
  }
}

TEST(ConfigUtilsTest, RequireMapAcceptsMapsAndRejectsOthers) {
  EXPECT_NO_THROW(requireMap(node("key: value"), "$"));
  EXPECT_THROW(requireMap(node("[a, b]"), "$"), ConfigError);
  YAML::Node undefined;
  EXPECT_THROW(requireMap(undefined, "$"), ConfigError);
}

TEST(ConfigUtilsTest, RequireSequenceAcceptsSequencesAndRejectsOthers) {
  EXPECT_NO_THROW(requireSequence(node("[a, b]"), "$"));
  EXPECT_THROW(requireSequence(node("key: value"), "$"), ConfigError);
}

TEST(ConfigUtilsTest, ScalarStringReturnsValue) { EXPECT_EQ(scalarString(node("hello"), "$"), "hello"); }

TEST(ConfigUtilsTest, ScalarStringRejectsNonScalar) { EXPECT_THROW(scalarString(node("[a]"), "$"), ConfigError); }

TEST(ConfigUtilsTest, RequiredStringReturnsValue) {
  EXPECT_EQ(requiredString(node("name: robot-a"), "name", "$"), "robot-a");
}

TEST(ConfigUtilsTest, RequiredStringRejectsMissing) {
  try {
    requiredString(node("other: 1"), "name", "$");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.context(), "$.name");
    EXPECT_EQ(e.detail(), "missing required field");
  }
}

TEST(ConfigUtilsTest, RequiredStringRejectsEmpty) {
  try {
    requiredString(node("name: \"\""), "name", "$");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.expected(), "nonempty string");
  }
}

TEST(ConfigUtilsTest, OptionalPositiveIntReturnsValue) { EXPECT_EQ(optionalPositiveInt(node("3"), "$"), 3); }

TEST(ConfigUtilsTest, OptionalPositiveIntRejectsNonPositive) {
  EXPECT_THROW(optionalPositiveInt(node("0"), "$"), ConfigError);
  EXPECT_THROW(optionalPositiveInt(node("-1"), "$"), ConfigError);
}

TEST(ConfigUtilsTest, OptionalPositiveIntRejectsNonInteger) {
  EXPECT_THROW(optionalPositiveInt(node("\"three\""), "$"), ConfigError);
  EXPECT_THROW(optionalPositiveInt(node("[1]"), "$"), ConfigError);
}

TEST(ConfigUtilsTest, MapKeyToStringReturnsScalarKey) {
  const auto map = node("alpha: 1");
  const auto key = map.begin()->first;
  EXPECT_EQ(mapKeyToString(key, "$"), "alpha");
}

TEST(ConfigUtilsTest, MapKeyToStringRejectsNonScalarKey) {
  const auto map = node("? [a, b]\n: 1");
  const auto key = map.begin()->first;
  try {
    mapKeyToString(key, "$");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.expected(), "string key");
  }
}

TEST(ConfigUtilsTest, RejectUnknownFieldsAcceptsAllowedSubset) {
  const std::set<std::string> allowed{"version", "topics"};
  EXPECT_NO_THROW(rejectUnknownFields(node("version: \"0.0.1\""), allowed, "$"));
}

TEST(ConfigUtilsTest, RejectUnknownFieldsRejectsUnknownKey) {
  const std::set<std::string> allowed{"version"};
  try {
    rejectUnknownFields(node("version: \"0.0.1\"\nextra: true"), allowed, "$");
    FAIL() << "Expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.detail(), "unknown field 'extra'");
  }
}

TEST(ConfigUtilsTest, RejectUnknownFieldsRejectsNonMap) {
  const std::set<std::string> allowed{"version"};
  EXPECT_THROW(rejectUnknownFields(node("[a, b]"), allowed, "$"), ConfigError);
}

} // namespace
} // namespace ros2_livekit_bridge_config::utils
