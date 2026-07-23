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

#ifdef ROS_DISTRO_HUMBLE
#include <ament_index_cpp/get_resource.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>
#else
#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>
#endif

#include <openssl/sha.h>

#include <array>
#include <exception>
#include <rclcpp/logging.hpp>
#include <stdexcept>
#include <utility>

#include "ros2_livekit_bridge/introspection/introspection_utils.hpp"

namespace ros2_livekit_bridge {

#ifdef ROS_DISTRO_HUMBLE
namespace {

enum class DefinitionFormat { kMsg, kIdl };

constexpr std::size_t kMaxDefinitionDepth = 50;
const std::regex kPackageTypeRegex{R"(^([a-zA-Z0-9_]+)(?:/[a-zA-Z0-9_]+)*/(?:msg/|srv/)?([a-zA-Z0-9_]+)$)"};
const std::regex kMsgFieldTypeRegex{R"((?:^|\n)\s*([a-zA-Z0-9_/]+)(?:\[[^\]]*\])?\s+)"};
const std::regex kIdlFieldTypeRegex{R"((?:^|\n)#include\s+(?:"|<)([a-zA-Z0-9_/]+)\.idl(?:"|>))"};
const std::unordered_set<std::string> kPrimitiveTypes{
    "bool",  "byte",   "char",  "float32", "float64", "int8",   "uint8",
    "int16", "uint16", "int32", "uint32",  "int64",   "uint64", "string",
};

struct DefinitionSpec {
  std::string package_name;
  std::string text;
};

std::string definitionExtension(DefinitionFormat format) { return format == DefinitionFormat::kMsg ? ".msg" : ".idl"; }

std::string definitionDelimiter(DefinitionFormat format, const std::string& type) {
  return "================================================================================\n" +
         std::string(format == DefinitionFormat::kMsg ? "MSG: " : "IDL: ") + type + "\n";
}

DefinitionSpec loadDefinition(const std::string& type, DefinitionFormat format) {
  std::smatch match;
  if (!std::regex_match(type, match, kPackageTypeRegex) || match.size() < 3) {
    throw std::runtime_error("Message type name is not understood: " + type);
  }

  const std::string package_name = match[1].str();
  const std::string file_name = match[2].str() + definitionExtension(format);
  std::string resource_content;
  std::string resource_prefix;
  if (!ament_index_cpp::get_resource("rosidl_interfaces", package_name, resource_content, &resource_prefix)) {
    throw std::runtime_error("Message package is not in the ament index: " + package_name);
  }

  std::istringstream resources(resource_content);
  std::string relative_path;
  for (std::string line; std::getline(resources, line);) {
    if (!line.empty() && std::filesystem::path(line).filename() == file_name) {
      relative_path = std::move(line);
      break;
    }
  }
  if (relative_path.empty()) {
    throw std::runtime_error("Message definition not found: " + type + definitionExtension(format));
  }

  const auto path = std::filesystem::path(resource_prefix) / "share" / package_name / relative_path;
  std::ifstream definition(path);
  if (!definition.good()) {
    throw std::runtime_error("Unable to read message definition: " + path.string());
  }
  return DefinitionSpec{package_name, std::string(std::istreambuf_iterator<char>(definition), {})};
}

std::set<std::string> definitionDependencies(const DefinitionSpec& spec, DefinitionFormat format) {
  std::set<std::string> dependencies;
  const std::regex& pattern = format == DefinitionFormat::kMsg ? kMsgFieldTypeRegex : kIdlFieldTypeRegex;
  for (std::sregex_iterator iterator(spec.text.begin(), spec.text.end(), pattern); iterator != std::sregex_iterator();
       ++iterator) {
    std::string type = (*iterator)[1].str();
    if (format == DefinitionFormat::kMsg && kPrimitiveTypes.count(type) != 0U) {
      continue;
    }
    if (format == DefinitionFormat::kMsg && type.find('/') == std::string::npos) {
      type = spec.package_name + "/" + type;
    }
    dependencies.insert(std::move(type));
  }
  return dependencies;
}

std::string appendDefinition(const std::string& type, DefinitionFormat format, std::unordered_set<std::string>& seen,
                             std::size_t depth) {
  if (depth == 0U) {
    throw std::runtime_error("Message definition exceeded maximum dependency depth: " + type);
  }

  const DefinitionSpec spec = loadDefinition(type, format);
  std::string result = spec.text;
  for (const auto& dependency : definitionDependencies(spec, format)) {
    if (seen.insert(dependency).second) {
      result += "\n" + definitionDelimiter(format, dependency) + appendDefinition(dependency, format, seen, depth - 1U);
    }
  }
  return result;
}

RosMessageSchema renderLegacyRosMessageSchema(const std::string& topic_type) {
  try {
    std::unordered_set<std::string> seen{topic_type};
    return RosMessageSchema{"ros2msg", appendDefinition(topic_type, DefinitionFormat::kMsg, seen, kMaxDefinitionDepth)};
  } catch (const std::runtime_error&) {
    std::unordered_set<std::string> seen{topic_type};
    return RosMessageSchema{"ros2idl",
                            definitionDelimiter(DefinitionFormat::kIdl, topic_type) +
                                appendDefinition(topic_type, DefinitionFormat::kIdl, seen, kMaxDefinitionDepth)};
  }
}

} // namespace
#endif

std::optional<RosMessageSchema> SchemaManager::renderRosMessageSchema(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }

  try {
#ifdef ROS_DISTRO_HUMBLE
    return renderLegacyRosMessageSchema(topic_type);
#else
    rosbag2_cpp::LocalMessageDefinitionSource source;
    const rosbag2_storage::MessageDefinition definition = source.get_full_text(topic_type);

    if (definition.encoded_message_definition.empty()) {
      return std::nullopt;
    }

    return RosMessageSchema{
        definition.encoding,
        definition.encoded_message_definition,
    };
#endif
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::string> SchemaManager::renderJsonSchema(const std::string& topic_type) {
  if (topic_type.empty()) {
    return std::nullopt;
  }
  std::string error;
  return introspection::renderJsonSchema(topic_type, error);
}

std::optional<livekit::DataTrackSchemaEncoding> SchemaManager::schemaEncodingFromRosDefinition(
    const std::string& encoding) {
  if (encoding == "ros2idl") {
    return livekit::DataTrackSchemaEncoding::Ros2Idl;
  }
  if (encoding == "ros2msg") {
    return livekit::DataTrackSchemaEncoding::Ros2Msg;
  }
  return std::nullopt;
}

std::string SchemaManager::schemaDedupeKey(const std::string& topic_type, const std::string& encoding) {
  return encoding + "\n" + topic_type;
}

SchemaHash SchemaManager::hashSchemaText(const std::string& schema_text) {
  static_assert(SchemaHash{}.size() == SHA256_DIGEST_LENGTH);

  SchemaHash hash{};
  SHA256(reinterpret_cast<const unsigned char*>(schema_text.data()), schema_text.size(), hash.data());
  return hash;
}

std::string SchemaManager::schemaHashToHex(const SchemaHash& hash) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  std::string hex(hash.size() * 2U, '0');
  for (std::size_t index = 0; index < hash.size(); ++index) {
    hex[index * 2U] = kHexDigits[hash[index] >> 4U];
    hex[index * 2U + 1U] = kHexDigits[hash[index] & 0x0FU];
  }
  return hex;
}

SchemaManager::SchemaManager(LiveKitMethods livekit_methods, RenderSchemaFn render_schema,
                             RenderJsonSchemaFn render_json_schema)
    : livekit_methods_(std::move(livekit_methods)),
      render_schema_(std::move(render_schema)),
      render_json_schema_(std::move(render_json_schema)),
      logger_(rclcpp::get_logger("schema_manager")) {
  if (!livekit_methods_.define_schema || !livekit_methods_.get_schema) {
    throw std::invalid_argument("SchemaManager requires fully populated LiveKitMethods");
  }
  if (!render_schema_) {
    render_schema_ = renderRosMessageSchema;
  }
  if (!render_json_schema_) {
    render_json_schema_ = renderJsonSchema;
  }
}

std::optional<livekit::DataTrackSchemaId> SchemaManager::ensureSchemaDefined(const std::string& topic_type,
                                                                             OutboundEncoding encoding) {
  std::string schema_text;
  livekit::DataTrackSchemaEncoding schema_encoding = livekit::DataTrackSchemaEncoding::Ros2Msg;
  std::string dedupe_encoding;

  if (encoding == OutboundEncoding::JsonSchema) {
    const auto json_schema = render_json_schema_(topic_type);
    if (!json_schema) {
      RCLCPP_ERROR(logger_, "Unable to generate required JSON Schema for type '%s'", topic_type.c_str());
      return std::nullopt;
    }
    schema_text = *json_schema;
    schema_encoding = livekit::DataTrackSchemaEncoding::JsonSchema;
    dedupe_encoding = "jsonschema";
  } else {
    const auto schema = render_schema_(topic_type);
    if (!schema) {
      RCLCPP_ERROR(logger_, "Unable to render required ROS schema for type '%s'", topic_type.c_str());
      return std::nullopt;
    }

    const auto rendered_encoding = schemaEncodingFromRosDefinition(schema->encoding);
    if (!rendered_encoding.has_value()) {
      RCLCPP_ERROR(logger_, "Unsupported ROS schema encoding '%s' for type '%s'", schema->encoding.c_str(),
                   topic_type.c_str());
      return std::nullopt;
    }

    // An explicit ros2idl request must render as ros2idl; otherwise skip the
    // topic rather than silently advertising a different schema encoding. The
    // default (ros2msg) path stays permissive and advertises whatever CDR
    // schema encoding rosbag2 renders, since the wire bytes are identical.
    if (encoding == OutboundEncoding::Ros2Idl && *rendered_encoding != livekit::DataTrackSchemaEncoding::Ros2Idl) {
      RCLCPP_ERROR(logger_,
                   "Requested outbound encoding 'ros2idl' for type '%s' but the local definition renders as '%s'; "
                   "skipping topic",
                   topic_type.c_str(), schema->encoding.c_str());
      return std::nullopt;
    }

    schema_text = schema->text;
    schema_encoding = *rendered_encoding;
    dedupe_encoding = schema->encoding;
  }

  livekit::DataTrackSchemaId schema_id{
      topic_type,
      schema_encoding,
  };
  const auto dedupe_key = schemaDedupeKey(topic_type, dedupe_encoding);
  const auto schema_hash = hashSchemaText(schema_text);

  {
    std::unique_lock<std::mutex> lock(defined_schemas_mutex_);
    while (true) {
      const auto existing = defined_schemas_.find(dedupe_key);
      if (existing == defined_schemas_.end()) {
        defined_schemas_.emplace(dedupe_key, DefinitionState{schema_hash, false});
        break;
      }
      if (existing->second.hash != schema_hash) {
        RCLCPP_ERROR(logger_, "Schema ID for type '%s' was already defined with a different hash", topic_type.c_str());
        return std::nullopt;
      }
      if (existing->second.defined) {
        return schema_id;
      }
      defined_schemas_cv_.wait(lock);
    }
  }

  bool defined = false;
  std::string error;
  try {
    defined = livekit_methods_.define_schema(schema_id, schema_text);
    if (!defined) {
      error = "LiveKit SDK rejected the schema definition";
    }
  } catch (const std::exception& exception) {
    error = exception.what();
  } catch (...) {
    error = "unknown schema definition error";
  }

  {
    const std::lock_guard<std::mutex> lock(defined_schemas_mutex_);
    const auto state = defined_schemas_.find(dedupe_key);
    if (defined) {
      state->second.defined = true;
    } else {
      defined_schemas_.erase(state);
    }
  }
  defined_schemas_cv_.notify_all();

  if (!defined) {
    RCLCPP_ERROR(logger_, "Failed to define required schema for type '%s': %s", topic_type.c_str(), error.c_str());
    return std::nullopt;
  }

  return schema_id;
}

bool SchemaManager::validateInboundSchema(const InboundSchemaContext& context) const {
  std::optional<SchemaHash> remote_hash;
  std::optional<SchemaHash> local_hash;
  const auto reject = [&](const std::string& reason) {
    const std::string remote_hash_text = remote_hash ? schemaHashToHex(*remote_hash) : "unavailable";
    const std::string local_hash_text = local_hash ? schemaHashToHex(*local_hash) : "unavailable";
    RCLCPP_ERROR(logger_,
                 "Rejecting LiveKit data track '%s' [%s] from '%s': %s "
                 "(remote_schema_sha256=%s local_schema_sha256=%s)",
                 context.track_name.c_str(), context.topic_type.c_str(), context.participant_identity.c_str(),
                 reason.c_str(), remote_hash_text.c_str(), local_hash_text.c_str());
    return false;
  };

  if (!context.frame_encoding.has_value()) {
    return reject("track does not advertise a frame encoding");
  }
  if (*context.frame_encoding != livekit::DataTrackFrameEncoding::Cdr &&
      *context.frame_encoding != livekit::DataTrackFrameEncoding::Json) {
    return reject("track frame encoding is not CDR or JSON");
  }
  if (!context.schema.has_value()) {
    return reject("track does not advertise a schema");
  }

  const auto& schema_id = *context.schema;
  if (schema_id.name != context.topic_type) {
    return reject("track schema name '" + schema_id.name + "' does not match local ROS type '" + context.topic_type +
                  "'");
  }

  if (schema_id.encoding == livekit::DataTrackSchemaEncoding::JsonSchema) {
    if (*context.frame_encoding != livekit::DataTrackFrameEncoding::Json) {
      return reject("JsonSchema tracks require JSON frame encoding");
    }
    if (!render_schema_(context.topic_type)) {
      return reject("local ROS schema could not be rendered");
    }
    RCLCPP_INFO(logger_, "Accepting LiveKit data track '%s' [%s] from '%s' via JsonSchema.", context.track_name.c_str(),
                context.topic_type.c_str(), context.participant_identity.c_str());
    return true;
  }

  if (schema_id.encoding != livekit::DataTrackSchemaEncoding::Ros2Msg &&
      schema_id.encoding != livekit::DataTrackSchemaEncoding::Ros2Idl) {
    return reject("track schema encoding is not ros2msg or ros2idl");
  }

  std::optional<std::string> remote_schema;
  try {
    remote_schema = livekit_methods_.get_schema(schema_id, context.participant_identity);
  } catch (const std::exception& error) {
    return reject("remote schema retrieval failed: " + std::string(error.what()));
  } catch (...) {
    return reject("remote schema retrieval failed with an unknown error");
  }
  if (!remote_schema.has_value()) {
    return reject("remote schema retrieval failed: schema is unavailable");
  }
  const auto& remote_schema_text = *remote_schema;
  remote_hash = hashSchemaText(remote_schema_text);

  const auto local_schema = render_schema_(context.topic_type);
  if (!local_schema) {
    return reject("local ROS schema could not be rendered");
  }
  local_hash = hashSchemaText(local_schema->text);

  const bool encoding_matches =
      (schema_id.encoding == livekit::DataTrackSchemaEncoding::Ros2Msg && local_schema->encoding == "ros2msg") ||
      (schema_id.encoding == livekit::DataTrackSchemaEncoding::Ros2Idl && local_schema->encoding == "ros2idl");
  if (!encoding_matches) {
    return reject("remote and local schema encodings differ");
  }
  if (remote_hash != local_hash || remote_schema_text != local_schema->text) {
    return reject("remote and local schema definitions differ");
  }

  return true;
}

} // namespace ros2_livekit_bridge
