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

#include "ros2_livekit_bridge/schema_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ros2_livekit_bridge {
namespace {

// Full recursive schema text in MCAP ros2msg format, as produced by
// rosbag2_cpp::LocalMessageDefinitionSource::get_full_text()

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

SchemaManager::LiveKitMethods makeLiveKitMethods() {
  SchemaManager::LiveKitMethods methods;
  methods.define_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return true; };
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  return methods;
}

SchemaManager::InboundSchemaContext makeInboundSchemaContext() {
  return {
      "/remote/data",
      "participant",
      "example_msgs/msg/Example",
      livekit::DataTrackSchemaId{"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::Ros2Msg},
      livekit::DataTrackFrameEncoding::Cdr,
  };
}

} // namespace

TEST(SchemaManagerTest, RenderRosMessageSchema) {
  const auto schema = SchemaManager::renderRosMessageSchema("std_msgs/msg/String");
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(schema->encoding, "ros2msg");
  EXPECT_EQ(schema->text, kStdMsgsStringSchemaText);

  EXPECT_FALSE(SchemaManager::renderRosMessageSchema("").has_value());
  EXPECT_FALSE(SchemaManager::renderRosMessageSchema("nonexistent_pkg/msg/DoesNotExist").has_value());
}

TEST(SchemaManagerTest, SchemaEncodingFromRosDefinition) {
  EXPECT_EQ(SchemaManager::schemaEncodingFromRosDefinition("ros2msg"), livekit::DataTrackSchemaEncoding::Ros2Msg);
  EXPECT_EQ(SchemaManager::schemaEncodingFromRosDefinition("ros2idl"), livekit::DataTrackSchemaEncoding::Ros2Idl);
  EXPECT_FALSE(SchemaManager::schemaEncodingFromRosDefinition("jsonschema").has_value());
}

TEST(SchemaManagerTest, SchemaDedupeKey) {
  EXPECT_EQ(SchemaManager::schemaDedupeKey("example_msgs/msg/Example", "ros2msg"), "ros2msg\nexample_msgs/msg/Example");
}

TEST(SchemaManagerTest, HashSchemaText) {
  const SchemaHash expected{
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
  };
  EXPECT_EQ(SchemaManager::hashSchemaText("abc"), expected);
}

TEST(SchemaManagerTest, SchemaHashToHex) {
  SchemaHash hash{};
  hash.front() = 0xab;
  hash.back() = 0xcd;
  std::string expected(64U, '0');
  expected.replace(0U, 2U, "ab");
  expected.replace(62U, 2U, "cd");

  EXPECT_EQ(SchemaManager::schemaHashToHex(hash), expected);
}

TEST(SchemaManagerTest, ConstructorRejectsMissingLiveKitMethods) {
  EXPECT_THROW(SchemaManager(SchemaManager::LiveKitMethods{}), std::invalid_argument);
}

TEST(SchemaManagerTest, RendersAndDefinesStdMsgsString) {
  auto methods = makeLiveKitMethods();
  livekit::DataTrackSchemaId defined_id;
  std::string defined_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId& schema_id, const std::string& text) {
    defined_id = schema_id;
    defined_text = text;
    return true;
  };
  SchemaManager manager(std::move(methods));

  const auto result = manager.ensureSchemaDefined("std_msgs/msg/String");

  ASSERT_TRUE(result);
  EXPECT_EQ(defined_id.name, "std_msgs/msg/String");
  EXPECT_EQ(defined_id.encoding, livekit::DataTrackSchemaEncoding::Ros2Msg);
  EXPECT_EQ(defined_text, kStdMsgsStringSchemaText);
}

TEST(SchemaManagerTest, RendersNestedDependencies) {
  auto methods = makeLiveKitMethods();
  std::string defined_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string& text) {
    defined_text = text;
    return true;
  };
  SchemaManager manager(std::move(methods));

  ASSERT_TRUE(manager.ensureSchemaDefined("geometry_msgs/msg/PoseStamped"));
  EXPECT_EQ(defined_text, kGeometryMsgsPoseStampedSchemaText);
}

TEST(SchemaManagerTest, RendersSensorMsgsImage) {
  auto methods = makeLiveKitMethods();
  std::string defined_text;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string& text) {
    defined_text = text;
    return true;
  };
  SchemaManager manager(std::move(methods));

  ASSERT_TRUE(manager.ensureSchemaDefined("sensor_msgs/msg/Image"));
  EXPECT_EQ(defined_text, kSensorMsgsImageSchemaText);
}

TEST(SchemaManagerTest, ReturnsFailureForUnknownType) {
  SchemaManager manager(makeLiveKitMethods());

  const auto result = manager.ensureSchemaDefined("nonexistent_pkg/msg/DoesNotExist");

  EXPECT_FALSE(result);
}

TEST(SchemaManagerTest, MapsKnownAndRejectsUnsupportedEncodings) {
  auto methods = makeLiveKitMethods();
  livekit::DataTrackSchemaId defined_id;
  methods.define_schema = [&](const livekit::DataTrackSchemaId& schema_id, const std::string&) {
    defined_id = schema_id;
    return true;
  };

  SchemaManager manager(std::move(methods),
                        [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2idl", "text"}}; });
  ASSERT_TRUE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  EXPECT_EQ(defined_id.encoding, livekit::DataTrackSchemaEncoding::Ros2Idl);

  int rejected_define_count = 0;
  auto unsupported_methods = makeLiveKitMethods();
  unsupported_methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++rejected_define_count;
    return true;
  };
  SchemaManager unsupported_manager(std::move(unsupported_methods), [](const std::string&) {
    return std::optional<RosMessageSchema>{{"custom_encoding", "text"}};
  });
  EXPECT_FALSE(unsupported_manager.ensureSchemaDefined("example_msgs/msg/Custom"));

  auto empty_methods = makeLiveKitMethods();
  empty_methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++rejected_define_count;
    return true;
  };
  SchemaManager empty_manager(std::move(empty_methods),
                              [](const std::string&) { return std::optional<RosMessageSchema>{{"", "text"}}; });
  EXPECT_FALSE(empty_manager.ensureSchemaDefined("example_msgs/msg/Empty"));
  EXPECT_EQ(rejected_define_count, 0);
}

TEST(SchemaManagerTest, DefinesAnExactSchemaOnlyOnce) {
  auto methods = makeLiveKitMethods();
  int define_count = 0;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++define_count;
    return true;
  };
  SchemaManager manager(std::move(methods), [](const std::string&) {
    return std::optional<RosMessageSchema>{{"ros2msg", "exact schema"}};
  });

  EXPECT_TRUE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  EXPECT_TRUE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  EXPECT_EQ(define_count, 1);
}

TEST(SchemaManagerTest, ConcurrentCallersShareOneDefinition) {
  constexpr int kCallerCount = 4;
  std::mutex gate_mutex;
  std::condition_variable gate_cv;
  int started_count = 0;
  int define_count = 0;
  std::atomic<int> success_count{0};

  auto methods = makeLiveKitMethods();
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    std::unique_lock<std::mutex> lock(gate_mutex);
    gate_cv.wait(lock, [&]() { return started_count == kCallerCount; });
    ++define_count;
    return true;
  };
  SchemaManager manager(std::move(methods), [](const std::string&) {
    return std::optional<RosMessageSchema>{{"ros2msg", "exact schema"}};
  });

  std::vector<std::thread> callers;
  callers.reserve(kCallerCount);
  for (int index = 0; index < kCallerCount; ++index) {
    callers.emplace_back([&]() {
      {
        const std::lock_guard<std::mutex> lock(gate_mutex);
        ++started_count;
      }
      gate_cv.notify_all();
      if (manager.ensureSchemaDefined("example_msgs/msg/Example")) {
        success_count.fetch_add(1);
      }
    });
  }
  for (auto& caller : callers) {
    caller.join();
  }

  EXPECT_EQ(success_count.load(), kCallerCount);
  EXPECT_EQ(define_count, 1);
}

TEST(SchemaManagerTest, RetriesAfterDefinitionFailure) {
  auto methods = makeLiveKitMethods();
  int define_count = 0;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++define_count;
    return define_count != 1;
  };
  SchemaManager manager(std::move(methods), [](const std::string&) {
    return std::optional<RosMessageSchema>{{"ros2msg", "exact schema"}};
  });

  EXPECT_FALSE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  EXPECT_TRUE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  EXPECT_EQ(define_count, 2);
}

TEST(SchemaManagerTest, RejectsChangedTextForAnExistingSchemaId) {
  auto methods = makeLiveKitMethods();
  int define_count = 0;
  methods.define_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++define_count;
    return true;
  };
  int render_count = 0;
  SchemaManager manager(std::move(methods), [&](const std::string&) {
    ++render_count;
    return std::optional<RosMessageSchema>{{"ros2msg", render_count == 1 ? "first" : "changed"}};
  });

  EXPECT_TRUE(manager.ensureSchemaDefined("example_msgs/msg/Example"));
  const auto changed_result = manager.ensureSchemaDefined("example_msgs/msg/Example");

  EXPECT_FALSE(changed_result);
  EXPECT_EQ(define_count, 1);
}

TEST(SchemaManagerTest, ValidatesExactInboundSchema) {
  auto methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"abc"};
  };
  const SchemaManager manager(std::move(methods),
                              [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "abc"}}; });

  auto context = makeInboundSchemaContext();
  EXPECT_TRUE(manager.validateInboundSchema(context));

  context.frame_encoding = livekit::DataTrackFrameEncoding::Json;
  EXPECT_TRUE(manager.validateInboundSchema(context));
}

TEST(SchemaManagerTest, RejectsMissingAndUnsupportedInboundMetadata) {
  const SchemaManager manager(
      makeLiveKitMethods(), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "schema"}}; });
  auto context = makeInboundSchemaContext();

  context.frame_encoding = std::nullopt;
  EXPECT_FALSE(manager.validateInboundSchema(context));

  context.frame_encoding = livekit::DataTrackFrameEncoding::Protobuf;
  EXPECT_FALSE(manager.validateInboundSchema(context));

  context.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;
  context.schema = std::nullopt;
  EXPECT_FALSE(manager.validateInboundSchema(context));
}

TEST(SchemaManagerTest, AcceptsJsonSchemaInteropWhenFrameEncodingIsJson) {
  int get_schema_count = 0;
  auto methods = makeLiveKitMethods();
  methods.get_schema = [&](const livekit::DataTrackSchemaId&, const std::string&) {
    ++get_schema_count;
    return std::optional<std::string>{"should not be fetched"};
  };

  int render_schema_count = 0;
  const SchemaManager manager(std::move(methods), [&](const std::string&) {
    ++render_schema_count;
    return std::optional<RosMessageSchema>{{"ros2msg", "schema"}};
  });

  auto context = makeInboundSchemaContext();
  context.schema = livekit::DataTrackSchemaId{"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::JsonSchema};
  context.frame_encoding = livekit::DataTrackFrameEncoding::Json;

  EXPECT_TRUE(manager.validateInboundSchema(context));
  EXPECT_EQ(get_schema_count, 0);
  EXPECT_EQ(render_schema_count, 1);
}

TEST(SchemaManagerTest, RejectsJsonSchemaWhenFrameEncodingIsNotJson) {
  const SchemaManager manager(
      makeLiveKitMethods(), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "schema"}}; });

  auto context = makeInboundSchemaContext();
  context.schema = livekit::DataTrackSchemaId{"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::JsonSchema};
  context.frame_encoding = livekit::DataTrackFrameEncoding::Cdr;

  EXPECT_FALSE(manager.validateInboundSchema(context));
}

TEST(SchemaManagerTest, RejectsUnsupportedEncodingAndWrongType) {
  const SchemaManager manager(
      makeLiveKitMethods(), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "schema"}}; });

  auto context = makeInboundSchemaContext();
  context.schema = livekit::DataTrackSchemaId{"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::Protobuf};
  EXPECT_FALSE(manager.validateInboundSchema(context));

  context.schema = livekit::DataTrackSchemaId{"example_msgs/msg/Other", livekit::DataTrackSchemaEncoding::Ros2Msg};
  EXPECT_FALSE(manager.validateInboundSchema(context));

  context.schema = livekit::DataTrackSchemaId{"example_msgs/msg/Other", livekit::DataTrackSchemaEncoding::JsonSchema};
  context.frame_encoding = livekit::DataTrackFrameEncoding::Json;
  EXPECT_FALSE(manager.validateInboundSchema(context));
}

TEST(SchemaManagerTest, RejectsRetrievalRenderEncodingAndTextFailures) {
  auto methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  SchemaManager manager(std::move(methods),
                        [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "local"}}; });
  const auto context = makeInboundSchemaContext();

  EXPECT_FALSE(manager.validateInboundSchema(context));

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"remote"};
  };
  SchemaManager render_failure_manager(std::move(methods),
                                       [](const std::string&) { return std::optional<RosMessageSchema>{}; });
  EXPECT_FALSE(render_failure_manager.validateInboundSchema(context));

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"same"};
  };
  SchemaManager encoding_mismatch_manager(
      std::move(methods), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2idl", "same"}}; });
  EXPECT_FALSE(encoding_mismatch_manager.validateInboundSchema(context));

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"remote"};
  };
  SchemaManager text_mismatch_manager(
      std::move(methods), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "local"}}; });
  EXPECT_FALSE(text_mismatch_manager.validateInboundSchema(context));
}

} // namespace ros2_livekit_bridge
