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

#include "ros2_livekit_bridge/schema/message_definition_source.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::schema {
namespace {

/// @brief Message types the two definition sources must agree on.
///
/// Covers the shapes that stress the bundled source's dependency traversal:
/// no dependencies, shared transitive dependencies, several dependencies at one
/// level (which pins the ordering), bounded and unbounded sequences, fixed
/// arrays, constants, defaults, deep nesting, and wide strings (which no `.msg`
/// definition can express, so both sources must fall back to `ros2idl`).
std::vector<std::string> comparisonCorpus() {
  return {
      "std_msgs/msg/String",
      "std_msgs/msg/Header",
      "builtin_interfaces/msg/Time",
      "sensor_msgs/msg/Image",
      "sensor_msgs/msg/PointCloud2",
      "sensor_msgs/msg/JointState",
      "geometry_msgs/msg/PoseStamped",
      "geometry_msgs/msg/TwistWithCovariance",
      "diagnostic_msgs/msg/DiagnosticArray",
      "nav_msgs/msg/Odometry",
      "nav_msgs/msg/Path",
      "test_msgs/msg/BasicTypes",
      "test_msgs/msg/Strings",
      "test_msgs/msg/WStrings",
      "test_msgs/msg/Constants",
      "test_msgs/msg/Defaults",
      "test_msgs/msg/Arrays",
      "test_msgs/msg/BoundedSequences",
      "test_msgs/msg/UnboundedSequences",
      "test_msgs/msg/Nested",
      "test_msgs/msg/MultiNested",
      "test_msgs/msg/Builtins",
  };
}

/// @brief Locate the first difference between two definitions.
///
/// Definitions run to several kilobytes and the corpus is large, so a plain
/// EXPECT_EQ on mismatch would bury CI logs in full text dumps. Report the line
/// number and just the two lines that differ instead.
std::string describeFirstDifference(const std::string& actual, const std::string& expected) {
  std::istringstream actual_lines(actual);
  std::istringstream expected_lines(expected);
  std::string actual_line;
  std::string expected_line;
  std::size_t line_number = 0;

  while (true) {
    const bool has_actual = static_cast<bool>(std::getline(actual_lines, actual_line));
    const bool has_expected = static_cast<bool>(std::getline(expected_lines, expected_line));
    if (!has_actual && !has_expected) {
      break;
    }
    ++line_number;
    if (has_actual != has_expected || actual_line != expected_line) {
      std::ostringstream report;
      report << "definitions differ at line " << line_number << " (bundled=" << actual.size()
             << " bytes, rosbag2=" << expected.size() << " bytes)\n"
             << "  bundled: " << (has_actual ? "[" + actual_line + "]" : "<end of text>") << "\n"
             << "  rosbag2: " << (has_expected ? "[" + expected_line + "]" : "<end of text>");
      return report.str();
    }
    actual_line.clear();
    expected_line.clear();
  }
  return {};
}

} // namespace

// Case: The bundled definition source must reproduce rosbag2's output exactly.
// Schema identity is a SHA-256 over the definition text, so a single byte of
// divergence changes schema hashes and makes a bridge using one source reject
// data tracks from a bridge using the other. Only distributions that ship the
// rosbag2 message-definition API can run this; Humble has no reference to
// compare against, and relies on this test passing everywhere else.
TEST(MessageDefinitionSourceTest, BundledSourceMatchesRosbag2ByteForByte) {
  if (!hasRosbag2DefinitionSource()) {
    GTEST_SKIP() << "This build has no rosbag2 message-definition API to compare against";
  }

  for (const auto& type : comparisonCorpus()) {
    SCOPED_TRACE("type=" + type);

    const auto bundled = renderBundledDefinition(type);
    const auto reference = renderRosbag2Definition(type);

    ASSERT_TRUE(reference.has_value()) << "rosbag2 could not render " << type;
    ASSERT_TRUE(bundled.has_value()) << "bundled source could not render " << type;
    EXPECT_EQ(bundled->encoding, reference->encoding);

    const auto difference = describeFirstDifference(bundled->text, reference->text);
    EXPECT_TRUE(difference.empty()) << difference;
  }
}

// Case: renderDefinition should resolve to whichever source this build has.
TEST(MessageDefinitionSourceTest, RenderDefinitionUsesTheAvailableSource) {
  const std::string type = "std_msgs/msg/String";

  const auto rendered = renderDefinition(type);
  ASSERT_TRUE(rendered.has_value());
  EXPECT_EQ(rendered->encoding, "ros2msg");

  const auto expected = hasRosbag2DefinitionSource() ? renderRosbag2Definition(type) : renderBundledDefinition(type);
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(rendered->text, expected->text);
}

// Case: Both sources should reject input they cannot resolve, rather than
// throwing or returning a partial definition.
TEST(MessageDefinitionSourceTest, UnresolvableTypesYieldNullopt) {
  for (const auto& type : {std::string{}, std::string{"nonexistent_pkg/msg/DoesNotExist"}}) {
    SCOPED_TRACE("type=[" + type + "]");
    EXPECT_FALSE(renderBundledDefinition(type).has_value());
    EXPECT_FALSE(renderDefinition(type).has_value());
    if (hasRosbag2DefinitionSource()) {
      EXPECT_FALSE(renderRosbag2Definition(type).has_value());
    }
  }
}

// Case: The bundled source is the only source on Humble, so it must work on
// every distribution regardless of which source renderDefinition selects.
TEST(MessageDefinitionSourceTest, BundledSourceRendersOnEveryDistribution) {
  const auto rendered = renderBundledDefinition("sensor_msgs/msg/Image");
  ASSERT_TRUE(rendered.has_value());
  EXPECT_EQ(rendered->encoding, "ros2msg");
  // The dependency sections rosbag2 would also emit, in the same order.
  EXPECT_NE(rendered->text.find("MSG: std_msgs/Header"), std::string::npos);
  EXPECT_NE(rendered->text.find("MSG: builtin_interfaces/Time"), std::string::npos);
  EXPECT_LT(rendered->text.find("MSG: std_msgs/Header"), rendered->text.find("MSG: builtin_interfaces/Time"));
}

} // namespace ros2_livekit_bridge::schema
