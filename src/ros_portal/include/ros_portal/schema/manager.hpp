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
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <rclcpp/logger.hpp>
#include <string>
#include <unordered_map>

#include "ros_portal/schema/renderer.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros_portal {

/// @brief Binary SHA-256 digest of exact schema text bytes.
/// @note The hash is intentionally a fixed-size array of 32 bytes and strings are only rendered for diagnostics.
using SchemaHash = std::array<std::uint8_t, 32U>;

/// @brief Wire and schema encoding selected for an outbound data track.
///
/// Chosen per topic via config `encoding`. @ref Ros2Msg and @ref Ros2Idl send
/// raw ROS CDR frames described by the matching schema encoding; @ref JsonSchema
/// sends JSON frames described by a generated JSON Schema.
enum class OutboundEncoding {
  /// @brief CDR frames described by a Ros2Msg schema (default).
  Ros2Msg,
  /// @brief CDR frames described by a Ros2Idl schema.
  Ros2Idl,
  /// @brief JSON frames described by a generated JSON Schema.
  JsonSchema,
};

/// @brief Renders, registers, and validates ROS message schemas for LiveKit data
/// tracks.
///
/// Successful outbound definitions are cached by schema ID so a participant
/// defines each exact schema only once. Failed definitions are not cached and
/// may be retried. The class is thread-safe.
class SchemaManager {
public:
  /// @brief Snapshot of counters reported under `schema.*`.
  struct DiagnosticsSnapshot {
    /// @brief Number of schema definitions currently cached or in flight.
    std::size_t definitions_active{0};
    /// @brief Schema definitions rejected by the SDK or an exception.
    std::uint64_t define_failures{0};
    /// @brief Local ROS or JSON schema render failures.
    std::uint64_t render_failures{0};
    /// @brief Explicit ros2idl requests skipped due to another local encoding.
    std::uint64_t encoding_mismatch_skips{0};
    /// @brief Inbound tracks missing a supported frame/schema encoding.
    std::uint64_t inbound_rejected_no_encoding{0};
    /// @brief Inbound tracks whose advertised type differs from the local type.
    std::uint64_t inbound_rejected_name_mismatch{0};
    /// @brief Inbound tracks whose remote definition could not be retrieved.
    std::uint64_t inbound_rejected_remote_unavailable{0};
    /// @brief Inbound tracks whose local and remote definitions differ.
    std::uint64_t inbound_rejected_definition_differs{0};
  };

  /// @brief LiveKit operations needed to store and retrieve schema definitions.
  struct LiveKitMethods {
    /// @brief Define schema text on the local LiveKit participant.
    std::function<bool(const livekit::DataTrackSchemaId&, const std::string&)> define_schema;
    /// @brief Retrieve schema text from a remote LiveKit participant.
    std::function<std::optional<std::string>(const livekit::DataTrackSchemaId&, const std::string&)> get_schema;
  };

  /// @brief Callback used to render a local ROS message definition.
  using RenderSchemaFn = std::function<std::optional<RosMessageSchema>(const std::string&)>;

  /// @brief Callback used to render a JSON Schema for a local ROS type. Returns
  /// the schema document text, or `std::nullopt` when the type cannot be
  /// resolved.
  using RenderJsonSchemaFn = std::function<std::optional<std::string>(const std::string&)>;

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
  /// When unset, the build-selected renderer uses the local ament index.
  /// @param render_json_schema Optional JSON Schema renderer override for
  /// deterministic tests. When unset, ROS introspection is used to generate the
  /// schema from the local ament index.
  /// @throws std::invalid_argument when either LiveKit callback is unset.
  explicit SchemaManager(LiveKitMethods livekit_methods, RenderSchemaFn render_schema = {},
                         RenderJsonSchemaFn render_json_schema = {});

  /// @brief Ensure the local participant has defined the schema for a ROS type.
  /// @param topic_type ROS message type in `pkg/msg/Type` form.
  /// @param encoding Outbound encoding selecting which schema to render and
  /// define. `Ros2Idl` requires the local definition to render as ROS 2 IDL and
  /// fails otherwise; `JsonSchema` generates a JSON Schema via introspection.
  /// @return The LiveKit schema ID on success, or `std::nullopt` after logging
  /// the failure.
  std::optional<livekit::DataTrackSchemaId> ensureSchemaDefined(const std::string& topic_type,
                                                                OutboundEncoding encoding = OutboundEncoding::Ros2Msg);

  /// @brief Validate and log an inbound track's schema against the locally
  /// rendered ROS definition.
  /// @param context Track, participant, schema, and frame-encoding metadata.
  /// @return True when the track metadata is compatible. `Ros2Msg` and
  /// `Ros2Idl` schemas require an exact local definition match. `JsonSchema`
  /// tracks are accepted when frame encoding is JSON, the schema name matches
  /// the resolved ROS type, and the local schema can be rendered; frame
  /// conversion uses local introspection only.
  bool validateInboundSchema(const InboundSchemaContext& context) const;

  /// @brief Snapshot schema definition and validation diagnostics.
  /// @return Current cache size and cumulative failure counters.
  DiagnosticsSnapshot diagnosticsSnapshot() const;

private:
#ifdef BUILD_TESTING
  FRIEND_TEST(SchemaManagerTest, SchemaEncodingFromRosDefinition);
  FRIEND_TEST(SchemaManagerTest, SchemaDedupeKey);
  FRIEND_TEST(SchemaManagerTest, HashSchemaText);
  FRIEND_TEST(SchemaManagerTest, SchemaHashToHex);
#endif

  /// @brief Generate a JSON Schema for a local ROS message type.
  static std::optional<std::string> renderJsonSchema(const std::string& topic_type);

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

  /// @brief Mutable counters consumed by TopicForwarder's diagnostic task.
  struct DiagnosticState {
    /// @brief Schema definitions rejected by the SDK or an exception.
    std::atomic<std::uint64_t> define_failures{0};
    /// @brief Local ROS or JSON schema render failures.
    std::atomic<std::uint64_t> render_failures{0};
    /// @brief Explicit ros2idl requests skipped due to another local encoding.
    std::atomic<std::uint64_t> encoding_mismatch_skips{0};
    /// @brief Inbound tracks missing a supported frame/schema encoding.
    std::atomic<std::uint64_t> inbound_rejected_no_encoding{0};
    /// @brief Inbound tracks whose advertised type differs from the local type.
    std::atomic<std::uint64_t> inbound_rejected_name_mismatch{0};
    /// @brief Inbound tracks whose remote definition could not be retrieved.
    std::atomic<std::uint64_t> inbound_rejected_remote_unavailable{0};
    /// @brief Inbound tracks whose local and remote definitions differ.
    std::atomic<std::uint64_t> inbound_rejected_definition_differs{0};
  };

  LiveKitMethods livekit_methods_;
  RenderSchemaFn render_schema_;
  RenderJsonSchemaFn render_json_schema_;
  rclcpp::Logger logger_;
  mutable std::mutex defined_schemas_mutex_;
  std::condition_variable defined_schemas_cv_;
  std::unordered_map<std::string, DefinitionState> defined_schemas_;
  mutable DiagnosticState diagnostic_state_;
};

} // namespace ros_portal
