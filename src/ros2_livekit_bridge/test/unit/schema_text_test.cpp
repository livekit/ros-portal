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

#include "ros2_livekit_bridge/utils/schema_text.hpp"

#include <gtest/gtest.h>

#include <string>

namespace ros2_livekit_bridge::utils {
namespace {

// Full recursive schema text in MCAP ros2msg format, as produced by
// rosbag2_cpp::LocalMessageDefinitionSource::get_full_text().
constexpr const char* kStdMsgsStringSchemaText = R"(# This was originally provided as an example message.
# It is deprecated as of Foxy
# It is recommended to create your own semantically meaningful message.
# However if you would like to continue using this please use the equivalent in example_msgs.

string data
)";

constexpr const char* kGeometryMsgsPoseStampedSchemaText = R"(# A Pose with reference coordinate frame and timestamp

std_msgs/Header header
Pose pose

================================================================================
MSG: geometry_msgs/Pose
# A representation of pose in free space, composed of position and orientation.

Point position
Quaternion orientation

================================================================================
MSG: geometry_msgs/Point
# This contains the position of a point in free space
float64 x
float64 y
float64 z

================================================================================
MSG: geometry_msgs/Quaternion
# This represents an orientation in free space in quaternion form.

float64 x 0
float64 y 0
float64 z 0
float64 w 1

================================================================================
MSG: std_msgs/Header
# Standard metadata for higher-level stamped data types.
# This is generally used to communicate timestamped data
# in a particular coordinate frame.

# Two-integer timestamp that is expressed as seconds and nanoseconds.
builtin_interfaces/Time stamp

# Transform frame with which this data is associated.
string frame_id

================================================================================
MSG: builtin_interfaces/Time
# This message communicates ROS Time defined here:
# https://design.ros2.org/articles/clock_and_time.html

# The seconds component, valid over all int32 values.
int32 sec

# The nanoseconds component, valid in the range [0, 1e9), to be added to the seconds component. 
# e.g.
# The time -1.7 seconds is represented as {sec: -2, nanosec: 3e8}
# The time 1.7 seconds is represented as {sec: 1, nanosec: 7e8}
uint32 nanosec
)";

constexpr const char* kSensorMsgsImageSchemaText = R"(# This message contains an uncompressed image
# (0, 0) is at top-left corner of image

std_msgs/Header header # Header timestamp should be acquisition time of image
                             # Header frame_id should be optical frame of camera
                             # origin of frame should be optical center of cameara
                             # +x should point to the right in the image
                             # +y should point down in the image
                             # +z should point into to plane of the image
                             # If the frame_id here and the frame_id of the CameraInfo
                             # message associated with the image conflict
                             # the behavior is undefined

uint32 height                # image height, that is, number of rows
uint32 width                 # image width, that is, number of columns

# The legal values for encoding are in file include/sensor_msgs/image_encodings.hpp
# If you want to standardize a new string format, join
# ros-users@lists.ros.org and send an email proposing a new encoding.

string encoding       # Encoding of pixels -- channel meaning, ordering, size
                      # taken from the list of strings in include/sensor_msgs/image_encodings.hpp

uint8 is_bigendian    # is this data bigendian?
uint32 step           # Full row length in bytes
uint8[] data          # actual matrix data, size is (step * rows)

================================================================================
MSG: std_msgs/Header
# Standard metadata for higher-level stamped data types.
# This is generally used to communicate timestamped data
# in a particular coordinate frame.

# Two-integer timestamp that is expressed as seconds and nanoseconds.
builtin_interfaces/Time stamp

# Transform frame with which this data is associated.
string frame_id

================================================================================
MSG: builtin_interfaces/Time
# This message communicates ROS Time defined here:
# https://design.ros2.org/articles/clock_and_time.html

# The seconds component, valid over all int32 values.
int32 sec

# The nanoseconds component, valid in the range [0, 1e9), to be added to the seconds component. 
# e.g.
# The time -1.7 seconds is represented as {sec: -2, nanosec: 3e8}
# The time 1.7 seconds is represented as {sec: 1, nanosec: 7e8}
uint32 nanosec
)";

TEST(SchemaTextTest, RendersStdMsgsString) {
  const auto schema = renderRosMessageSchema("std_msgs/msg/String");
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->encoding, "ros2msg");
  EXPECT_EQ(schema->text, kStdMsgsStringSchemaText);
}

TEST(SchemaTextTest, RendersNestedDependencies) {
  const auto schema = renderRosMessageSchema("geometry_msgs/msg/PoseStamped");
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->encoding, "ros2msg");
  EXPECT_EQ(schema->text, kGeometryMsgsPoseStampedSchemaText);
}

// TODO: double nested payload

TEST(SchemaTextTest, RendersSensorMsgsImage) {
  const auto schema = renderRosMessageSchema("sensor_msgs/msg/Image");
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->encoding, "ros2msg");
  EXPECT_EQ(schema->text, kSensorMsgsImageSchemaText);
}

TEST(SchemaTextTest, ReturnsNulloptForUnknownType) {
  const auto schema = renderRosMessageSchema("nonexistent_pkg/msg/DoesNotExist");
  EXPECT_FALSE(schema.has_value());
}

TEST(SchemaTextTest, FingerprintUsesStableSha256Hex) {
  EXPECT_EQ(fingerprintSchemaText(""),
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(fingerprintSchemaText("abc"),
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad");
}

TEST(SchemaTextTest, FingerprintChangesForRootAndNestedDefinitionChanges) {
  const std::string original = kGeometryMsgsPoseStampedSchemaText;

  std::string changed_root = original;
  const auto root_field = changed_root.find("Pose pose");
  ASSERT_NE(root_field, std::string::npos);
  changed_root.replace(root_field, std::string("Pose pose").size(), "Pose other_pose");

  std::string changed_nested = original;
  const auto nested_field = changed_nested.find("float64 x");
  ASSERT_NE(nested_field, std::string::npos);
  changed_nested.replace(nested_field, std::string("float64 x").size(), "float32 x");

  EXPECT_EQ(fingerprintSchemaText(original), fingerprintSchemaText(original));
  EXPECT_NE(fingerprintSchemaText(original), fingerprintSchemaText(changed_root));
  EXPECT_NE(fingerprintSchemaText(original), fingerprintSchemaText(changed_nested));
}

} // namespace
} // namespace ros2_livekit_bridge::utils
