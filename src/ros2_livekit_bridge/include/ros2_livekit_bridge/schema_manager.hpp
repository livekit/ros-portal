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

#include <livekit/data_track_schema.h>
#include <livekit/result.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

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
  std::string encoding;
  /// @brief Full concatenated message definition text.
  std::string text;
};

/// @brief Binary SHA-256 digest of exact schema text bytes.
/// @note The hash is intentionally a fixed-size array of 32 bytes and strings are only rendered for diagnostics.
using SchemaHash = std::array<std::uint8_t, 32U>;

/// @brief Format a binary schema hash for diagnostics.
/// @param hash Binary SHA-256 schema digest.
/// @return Lowercase hexadecimal text.
std::string schemaHashToHex(const SchemaHash& hash);

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

  /// @brief Result of validating remote schema metadata against local text.
  struct ValidationResult {
    /// @brief Whether the remote schema is safe to use.
    bool accepted{false};
    /// @brief Human-readable rejection reason.
    std::string reason;
    /// @brief Binary SHA-256 digest of the retrieved remote schema, when
    /// available.
    std::optional<SchemaHash> remote_hash;
    /// @brief Binary SHA-256 digest of the rendered local schema, when
    /// available.
    std::optional<SchemaHash> local_hash;
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
  /// @return The LiveKit schema ID on success, or an explanatory error.
  livekit::Result<livekit::DataTrackSchemaId, std::string> ensureSchemaDefined(const std::string& topic_type);

  /// @brief Validate a remote schema against the locally rendered ROS
  /// definition.
  /// @param schema_id Schema metadata advertised by the remote data track.
  /// @param participant_identity Identity used to retrieve the remote schema.
  /// @param topic_type Locally resolved ROS message type.
  /// @return Validation result including exact-text hashes.
  ValidationResult validateInboundSchema(const livekit::DataTrackSchemaId& schema_id,
                                         const std::string& participant_identity, const std::string& topic_type) const;

private:
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
  std::mutex defined_schemas_mutex_;
  std::condition_variable defined_schemas_cv_;
  std::unordered_map<std::string, DefinitionState> defined_schemas_;
};

} // namespace ros2_livekit_bridge
