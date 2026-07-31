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

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "ros_portal/schema/renderer.hpp"
#include "schema/renderer_backend.hpp"

namespace ros_portal::schema {
namespace {

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

// Schema IDs hash the exact definition bytes, so compare both renderers wherever
// rosbag2 provides the reference API.
TEST(SchemaRendererTest, BundledRendererMatchesRosbag2ByteForByte) {
  if (!detail::renderWithBackend("std_msgs/msg/String", detail::RendererBackend::Rosbag2)) {
    GTEST_SKIP() << "This build has no rosbag2 message-definition API to compare against";
  }

  for (const auto& type : comparisonCorpus()) {
    SCOPED_TRACE("type=" + type);

    const auto bundled = detail::renderWithBackend(type, detail::RendererBackend::Bundled);
    const auto reference = detail::renderWithBackend(type, detail::RendererBackend::Rosbag2);

    ASSERT_TRUE(reference.has_value()) << "rosbag2 could not render " << type;
    ASSERT_TRUE(bundled.has_value()) << "bundled source could not render " << type;
    EXPECT_EQ(bundled->encoding, reference->encoding);

    const auto difference = describeFirstDifference(bundled->text, reference->text);
    EXPECT_TRUE(difference.empty()) << difference;
  }
}

TEST(SchemaRendererTest, RenderRosMessageSchemaUsesTheAvailableRenderer) {
  const std::string type = "std_msgs/msg/String";

  const auto rendered = renderRosMessageSchema(type);
  ASSERT_TRUE(rendered.has_value());
  EXPECT_EQ(rendered->encoding, "ros2msg");

  auto expected = detail::renderWithBackend(type, detail::RendererBackend::Rosbag2);
  if (!expected) {
    expected = detail::renderWithBackend(type, detail::RendererBackend::Bundled);
  }
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(rendered->text, expected->text);
}

// Both renderers should reject input they cannot resolve, rather than
// throwing or returning a partial definition.
TEST(SchemaRendererTest, UnresolvableTypesYieldNullopt) {
  for (const auto& type : {std::string{}, std::string{"nonexistent_pkg/msg/DoesNotExist"}}) {
    SCOPED_TRACE("type=[" + type + "]");
    EXPECT_FALSE(detail::renderWithBackend(type, detail::RendererBackend::Bundled).has_value());
    EXPECT_FALSE(renderRosMessageSchema(type).has_value());
    EXPECT_FALSE(detail::renderWithBackend(type, detail::RendererBackend::Rosbag2).has_value());
  }
}

// Case: The bundled source is the only source on Humble, so it must work on
// every distribution regardless of which renderer production selects.
TEST(SchemaRendererTest, BundledRendererWorksOnEveryDistribution) {
  const auto rendered = detail::renderWithBackend("sensor_msgs/msg/Image", detail::RendererBackend::Bundled);
  ASSERT_TRUE(rendered.has_value());
  EXPECT_EQ(rendered->encoding, "ros2msg");
  // The dependency sections rosbag2 would also emit, in the same order.
  EXPECT_NE(rendered->text.find("MSG: std_msgs/Header"), std::string::npos);
  EXPECT_NE(rendered->text.find("MSG: builtin_interfaces/Time"), std::string::npos);
  EXPECT_LT(rendered->text.find("MSG: std_msgs/Header"), rendered->text.find("MSG: builtin_interfaces/Time"));
}

} // namespace ros_portal::schema
