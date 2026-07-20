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

#include "ros2_livekit_bridge/message_schema.hpp"

#include <rcutils/sha256.h>

#include <array>
#include <cstdint>
#include <exception>
#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>

namespace ros2_livekit_bridge {

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

std::string fingerprintSchemaText(const std::string& schema_text) {
  rcutils_sha256_ctx_t context;
  rcutils_sha256_init(&context);
  rcutils_sha256_update(&context, reinterpret_cast<const std::uint8_t*>(schema_text.data()), schema_text.size());

  std::array<std::uint8_t, RCUTILS_SHA256_BLOCK_SIZE> digest{};
  rcutils_sha256_final(&context, digest.data());

  constexpr char kHexDigits[] = "0123456789abcdef";
  std::string fingerprint(digest.size() * 2U, '0');
  for (std::size_t index = 0; index < digest.size(); ++index) {
    fingerprint[index * 2U] = kHexDigits[digest[index] >> 4U];
    fingerprint[index * 2U + 1U] = kHexDigits[digest[index] & 0x0FU];
  }
  return fingerprint;
}

} // namespace ros2_livekit_bridge
