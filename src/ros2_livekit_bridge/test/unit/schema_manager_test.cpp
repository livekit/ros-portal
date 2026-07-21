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
  EXPECT_NE(result.error().find("unable to render"), std::string::npos);
}

TEST(SchemaManagerTest, MapsKnownAndCustomEncodings) {
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

  auto custom_methods = makeLiveKitMethods();
  custom_methods.define_schema = [&](const livekit::DataTrackSchemaId& schema_id, const std::string&) {
    defined_id = schema_id;
    return true;
  };
  SchemaManager custom_manager(std::move(custom_methods), [](const std::string&) {
    return std::optional<RosMessageSchema>{{"custom_encoding", "text"}};
  });
  ASSERT_TRUE(custom_manager.ensureSchemaDefined("example_msgs/msg/Custom"));
  EXPECT_EQ(defined_id.encoding, livekit::DataTrackSchemaEncoding::custom("custom_encoding"));
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
  EXPECT_NE(changed_result.error().find("different hash"), std::string::npos);
  EXPECT_EQ(define_count, 1);
}

TEST(SchemaManagerTest, ValidatesExactInboundSchemaAndReportsStableHash) {
  auto methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"abc"};
  };
  const SchemaManager manager(std::move(methods),
                              [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "abc"}}; });

  const auto result =
      manager.validateInboundSchema({"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::Ros2Msg},
                                    "participant", "example_msgs/msg/Example");

  EXPECT_TRUE(result.accepted);
  ASSERT_TRUE(result.remote_hash.has_value());
  const auto remote_hash_hex = schemaHashToHex(*result.remote_hash);
  EXPECT_EQ(remote_hash_hex,
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(result.remote_hash, result.local_hash);
}

TEST(SchemaManagerTest, RejectsUnsupportedEncodingAndWrongType) {
  const SchemaManager manager(
      makeLiveKitMethods(), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "schema"}}; });

  auto result =
      manager.validateInboundSchema({"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::JsonSchema},
                                    "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("encoding"), std::string::npos);

  result = manager.validateInboundSchema({"example_msgs/msg/Other", livekit::DataTrackSchemaEncoding::Ros2Msg},
                                         "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("does not match"), std::string::npos);
}

TEST(SchemaManagerTest, RejectsRetrievalRenderEncodingAndTextFailures) {
  auto methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) { return std::nullopt; };
  SchemaManager manager(std::move(methods),
                        [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "local"}}; });
  const livekit::DataTrackSchemaId schema_id{"example_msgs/msg/Example", livekit::DataTrackSchemaEncoding::Ros2Msg};

  auto result = manager.validateInboundSchema(schema_id, "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("retrieval failed"), std::string::npos);

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"remote"};
  };
  SchemaManager render_failure_manager(std::move(methods),
                                       [](const std::string&) { return std::optional<RosMessageSchema>{}; });
  result = render_failure_manager.validateInboundSchema(schema_id, "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("could not be rendered"), std::string::npos);

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"same"};
  };
  SchemaManager encoding_mismatch_manager(
      std::move(methods), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2idl", "same"}}; });
  result = encoding_mismatch_manager.validateInboundSchema(schema_id, "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("encodings differ"), std::string::npos);

  methods = makeLiveKitMethods();
  methods.get_schema = [](const livekit::DataTrackSchemaId&, const std::string&) {
    return std::optional<std::string>{"remote"};
  };
  SchemaManager text_mismatch_manager(
      std::move(methods), [](const std::string&) { return std::optional<RosMessageSchema>{{"ros2msg", "local"}}; });
  result = text_mismatch_manager.validateInboundSchema(schema_id, "participant", "example_msgs/msg/Example");
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reason.find("definitions differ"), std::string::npos);
  EXPECT_NE(result.remote_hash, result.local_hash);
}

} // namespace
} // namespace ros2_livekit_bridge
