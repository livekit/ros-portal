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

#pragma once

#include <livekit/data_track_frame.h>
#include <livekit/data_track_schema.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <rclcpp/logger.hpp>
#include <string>
#include <unordered_map>

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros2_livekit_bridge {

/// @brief A self-contained ROS2 message schema suitable for LiveKit data tracks.
///
/// The @p encoding field is the rosbag2 definition encoding (for example
/// `"ros2msg"` or `"ros2idl"`). The @p text field is the full concatenated
/// definition text in MCAP format, including dependency sections separated by
/// `================================================================================`
/// delimiters.
struct RosMessageSchema {
  /// @brief Definition encoding, e.g. `"ros2msg"` or `"ros2idl"`.
  /// @note String because rosbag2 returns encoding as one.
  std::string encoding;
  /// @brief Full concatenated message definition text.
  std::string text;
};

/// @brief Binary SHA-256 digest of exact schema text bytes.
/// @note The hash is intentionally a fixed-size array of 32 bytes and strings are only rendered for diagnostics.
using SchemaHash = std::array<std::uint8_t, 32U>;

/// @brief Renders, registers, and validates ROS message schemas for LiveKit data
/// tracks.
///
/// Successful outbound definitions are cached by schema ID so a participant
/// defines each exact schema only once. Failed definitions are not cached and
/// may be retried. The class is thread-safe.
class SchemaManager {
public:
  /// @brief LiveKit operations needed to store and retrieve schema definitions.
  struct LiveKitMethods {
    /// @brief Define schema text on the local LiveKit participant.
    std::function<bool(const livekit::DataTrackSchemaId&, const std::string&)> define_schema;
    /// @brief Retrieve schema text from a remote LiveKit participant.
    std::function<std::optional<std::string>(const livekit::DataTrackSchemaId&, const std::string&)> get_schema;
  };

  /// @brief Callback used to render a local ROS message definition.
  using RenderSchemaFn = std::function<std::optional<RosMessageSchema>(const std::string&)>;

  /// @brief Metadata needed to validate an inbound LiveKit data track's ROS
  /// schema.
  struct InboundSchemaContext {
    /// @brief LiveKit track name used in rejection diagnostics.
    std::string track_name;
    /// @brief Identity used to retrieve the remote schema.
    std::string participant_identity;
    /// @brief Locally resolved ROS message type.
    std::string topic_type;
    /// @brief Schema metadata advertised by the remote data track.
    std::optional<livekit::DataTrackSchemaId> schema;
    /// @brief Wire encoding advertised by the remote data track.
    std::optional<livekit::DataTrackFrameEncoding> frame_encoding;
  };

  /// @brief Construct a schema manager.
  /// @param livekit_methods LiveKit schema definition and retrieval operations.
  /// @param render_schema Optional renderer override for deterministic tests.
  /// When unset, rosbag2 is used to render definitions from the local ament
  /// index.
  /// @throws std::invalid_argument when either LiveKit callback is unset.
  explicit SchemaManager(LiveKitMethods livekit_methods, RenderSchemaFn render_schema = {});

  /// @brief Ensure the local participant has defined the schema for a ROS type.
  /// @param topic_type ROS message type in `pkg/msg/Type` form.
  /// @return The LiveKit schema ID on success, or `std::nullopt` after logging
  /// the failure.
  std::optional<livekit::DataTrackSchemaId> ensureSchemaDefined(const std::string& topic_type);

  /// @brief Validate and log an inbound track's schema against the locally
  /// rendered ROS definition.
  /// @param context Track, participant, schema, and frame-encoding metadata.
  /// @return True when the track metadata is compatible. `Ros2Msg` and
  /// `Ros2Idl` schemas require an exact local definition match. `JsonSchema`
  /// tracks are accepted when frame encoding is JSON, the schema name matches
  /// the resolved ROS type, and the local schema can be rendered; frame
  /// conversion uses local introspection only.
  bool validateInboundSchema(const InboundSchemaContext& context) const;

private:
#ifdef BUILD_TESTING
  FRIEND_TEST(SchemaManagerTest, RenderRosMessageSchema);
  FRIEND_TEST(SchemaManagerTest, SchemaEncodingFromRosDefinition);
  FRIEND_TEST(SchemaManagerTest, SchemaDedupeKey);
  FRIEND_TEST(SchemaManagerTest, HashSchemaText);
  FRIEND_TEST(SchemaManagerTest, SchemaHashToHex);
#endif

  /// @brief Render a local ROS message definition and its dependencies.
  static std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type);

  /// @brief Map a rosbag2 schema encoding to the LiveKit equivalent.
  static std::optional<livekit::DataTrackSchemaEncoding> schemaEncodingFromRosDefinition(const std::string& encoding);

  /// @brief Build the cache key for a ROS type and schema encoding.
  static std::string schemaDedupeKey(const std::string& topic_type, const std::string& encoding);

  /// @brief Compute the binary SHA-256 digest of exact schema bytes.
  static SchemaHash hashSchemaText(const std::string& schema_text);

  /// @brief Format a binary schema hash as lowercase hexadecimal text.
  static std::string schemaHashToHex(const SchemaHash& hash);

  // Tracks one schema definition across concurrent ensureSchemaDefined calls.
  struct DefinitionState {
    // Binary SHA-256 digest of the exact schema text reserved for this schema
    // ID.
    SchemaHash hash;
    // False while one caller is defining the schema; true after it succeeds.
    bool defined{false};
  };

  LiveKitMethods livekit_methods_;
  RenderSchemaFn render_schema_;
  rclcpp::Logger logger_;
  std::mutex defined_schemas_mutex_;
  std::condition_variable defined_schemas_cv_;
  std::unordered_map<std::string, DefinitionState> defined_schemas_;
};

} // namespace ros2_livekit_bridge
