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

#include <rcutils/sha256.h>

#include <array>
#include <cstdint>
#include <exception>
#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>
#include <stdexcept>
#include <utility>

namespace ros2_livekit_bridge {
namespace {

std::optional<RosMessageSchema> renderRosMessageSchema(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }

  try {
    rosbag2_cpp::LocalMessageDefinitionSource source;
    const rosbag2_storage::MessageDefinition definition = source.get_full_text(topic_type);

    if (definition.encoded_message_definition.empty()) {
      return std::nullopt;
    }

    return RosMessageSchema{
        definition.encoding,
        definition.encoded_message_definition,
    };
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

livekit::DataTrackSchemaEncoding schemaEncodingFromRosDefinition(const std::string& encoding) {
  if (encoding == "ros2idl") {
    return livekit::DataTrackSchemaEncoding::Ros2Idl;
  }
  if (encoding == "ros2msg") {
    return livekit::DataTrackSchemaEncoding::Ros2Msg;
  }
  if (!encoding.empty() && encoding.size() <= 25U) {
    return livekit::DataTrackSchemaEncoding::custom(encoding);
  }
  return livekit::DataTrackSchemaEncoding::Ros2Msg;
}

std::string schemaDedupeKey(const std::string& topic_type, const std::string& encoding) {
  return encoding + "\n" + topic_type;
}

/// @brief Compute the binary SHA-256 digest of exact schema bytes.
SchemaHash hashSchemaText(const std::string& schema_text) {
  static_assert(SchemaHash{}.size() == RCUTILS_SHA256_BLOCK_SIZE);

  rcutils_sha256_ctx_t context;
  rcutils_sha256_init(&context);
  rcutils_sha256_update(&context, reinterpret_cast<const std::uint8_t*>(schema_text.data()), schema_text.size());

  SchemaHash hash{};
  rcutils_sha256_final(&context, hash.data());
  return hash;
}

} // namespace

std::string schemaHashToHex(const SchemaHash& hash) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex(hash.size() * 2U, '0');
  for (std::size_t index = 0; index < hash.size(); ++index) {
    hex[index * 2U] = kHexDigits[hash[index] >> 4U];
    hex[index * 2U + 1U] = kHexDigits[hash[index] & 0x0FU];
  }
  return hex;
}

SchemaManager::SchemaManager(LiveKitMethods livekit_methods, RenderSchema render_schema)
    : livekit_methods_(std::move(livekit_methods)), render_schema_(std::move(render_schema)) {
  if (!livekit_methods_.define_schema || !livekit_methods_.get_schema) {
    throw std::invalid_argument("SchemaManager requires fully populated LiveKitMethods");
  }
  if (!render_schema_) {
    render_schema_ = renderRosMessageSchema;
  }
}

livekit::Result<livekit::DataTrackSchemaId, std::string> SchemaManager::ensureSchemaDefined(
    const std::string& topic_type) {
  const auto schema = render_schema_(topic_type);
  if (!schema) {
    return livekit::Result<livekit::DataTrackSchemaId, std::string>::failure(
        "unable to render required ROS schema for type '" + topic_type + "'");
  }

  const livekit::DataTrackSchemaId schema_id{
      topic_type,
      schemaEncodingFromRosDefinition(schema->encoding),
  };
  const auto dedupe_key = schemaDedupeKey(topic_type, schema->encoding);
  const auto schema_hash = hashSchemaText(schema->text);

  {
    std::unique_lock<std::mutex> lock(defined_schemas_mutex_);
    while (true) {
      const auto existing = defined_schemas_.find(dedupe_key);
      if (existing == defined_schemas_.end()) {
        defined_schemas_.emplace(dedupe_key, DefinitionState{schema_hash, false});
        break;
      }
      if (existing->second.hash != schema_hash) {
        return livekit::Result<livekit::DataTrackSchemaId, std::string>::failure(
            "schema ID for type '" + topic_type + "' was already defined with a different hash");
      }
      if (existing->second.defined) {
        return livekit::Result<livekit::DataTrackSchemaId, std::string>::success(schema_id);
      }
      defined_schemas_cv_.wait(lock);
    }
  }

  const auto define_result = [&]() -> livekit::Result<void, std::string> {
    try {
      return livekit_methods_.define_schema(schema_id, schema->text);
    } catch (const std::exception& error) {
      return livekit::Result<void, std::string>::failure(error.what());
    } catch (...) {
      return livekit::Result<void, std::string>::failure("unknown schema definition error");
    }
  }();

  {
    const std::lock_guard<std::mutex> lock(defined_schemas_mutex_);
    const auto state = defined_schemas_.find(dedupe_key);
    if (define_result) {
      state->second.defined = true;
    } else {
      defined_schemas_.erase(state);
    }
  }
  defined_schemas_cv_.notify_all();

  if (!define_result) {
    return livekit::Result<livekit::DataTrackSchemaId, std::string>::failure(
        "failed to define required schema for type '" + topic_type + "': " + define_result.error());
  }

  return livekit::Result<livekit::DataTrackSchemaId, std::string>::success(schema_id);
}

SchemaManager::ValidationResult SchemaManager::validateInboundSchema(const livekit::DataTrackSchemaId& schema_id,
                                                                     const std::string& participant_identity,
                                                                     const std::string& topic_type) const {
  ValidationResult validation;
  if (schema_id.encoding != livekit::DataTrackSchemaEncoding::Ros2Msg &&
      schema_id.encoding != livekit::DataTrackSchemaEncoding::Ros2Idl) {
    validation.reason = "track schema encoding is not ros2msg or ros2idl";
    return validation;
  }
  if (schema_id.name != topic_type) {
    validation.reason = "track schema name '" + schema_id.name + "' does not match local ROS type '" + topic_type + "'";
    return validation;
  }

  const auto remote_schema_result = livekit_methods_.get_schema(schema_id, participant_identity);
  if (!remote_schema_result) {
    validation.reason = "remote schema retrieval failed: " + remote_schema_result.error();
    return validation;
  }
  const auto& remote_schema_text = remote_schema_result.value();
  validation.remote_hash = hashSchemaText(remote_schema_text);

  const auto local_schema = render_schema_(topic_type);
  if (!local_schema) {
    validation.reason = "local ROS schema could not be rendered";
    return validation;
  }
  validation.local_hash = hashSchemaText(local_schema->text);

  const bool encoding_matches =
      (schema_id.encoding == livekit::DataTrackSchemaEncoding::Ros2Msg && local_schema->encoding == "ros2msg") ||
      (schema_id.encoding == livekit::DataTrackSchemaEncoding::Ros2Idl && local_schema->encoding == "ros2idl");
  if (!encoding_matches) {
    validation.reason = "remote and local schema encodings differ";
    return validation;
  }
  if (validation.remote_hash != validation.local_hash || remote_schema_text != local_schema->text) {
    validation.reason = "remote and local schema definitions differ";
    return validation;
  }

  validation.accepted = true;
  return validation;
}

} // namespace ros2_livekit_bridge
