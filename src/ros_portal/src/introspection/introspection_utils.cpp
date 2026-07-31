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

#include "ros_portal/introspection/introspection_utils.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <ros2_medkit_serialization/json_serializer.hpp>
#include <ros2_medkit_serialization/type_cache.hpp>
#include <ros2_medkit_serialization/vendored/dynmsg/message_reading.hpp>
#include <ros2_medkit_serialization/vendored/dynmsg/yaml_utils.hpp>
#include <string>

#include "ros_portal/cli/constants.hpp"

namespace ros_portal::introspection {
namespace {

bool containsOversizedSequence(const nlohmann::json& value) {
  if (value.is_array()) {
    if (value.size() > cli::kMaxResizableSequenceLength) {
      return true;
    }
    for (const auto& entry : value) {
      if (containsOversizedSequence(entry)) {
        return true;
      }
    }
    return false;
  }

  if (value.is_object()) {
    for (const auto& entry : value.items()) {
      if (containsOversizedSequence(entry.value())) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

bool containsOversizedSequence(const YAML::Node& node) {
  if (node.IsSequence()) {
    if (node.size() > cli::kMaxResizableSequenceLength) {
      return true;
    }
    for (const auto& entry : node) {
      if (containsOversizedSequence(entry)) {
        return true;
      }
    }
    return false;
  }

  if (node.IsMap()) {
    for (const auto& entry : node) {
      if (containsOversizedSequence(entry.second)) {
        return true;
      }
    }
  }
  return false;
}

std::optional<YAML::Node> loadPayload(const std::string& payload, std::string& error) {
  if (payload.empty()) {
    error = "payload must be non-empty";
    return std::nullopt;
  }
  if (payload.size() > cli::kMaxYamlPayloadBytes) {
    error = "payload exceeds maximum size of " + std::to_string(cli::kMaxYamlPayloadBytes) + " bytes";
    return std::nullopt;
  }

  try {
    auto root = YAML::Load(payload);
    if (containsOversizedSequence(root)) {
      error =
          "payload contains a sequence exceeding maximum length " + std::to_string(cli::kMaxResizableSequenceLength);
      return std::nullopt;
    }
    return root;
  } catch (const YAML::Exception& parse_error) {
    error = std::string("payload is not valid YAML: ") + parse_error.what();
    return std::nullopt;
  }
}

std::optional<std::string> toYaml(const std::string& msg_type, const void* message) {
  const auto* type_info = ros2_medkit_serialization::TypeCache::instance().get_message_type_info(msg_type);
  if (type_info == nullptr) {
    return std::nullopt;
  }
  return toYaml(*type_info, message);
}

std::string toYaml(const rosidl_typesupport_introspection_cpp::MessageMembers& members, const void* message) {
  // dynmsg renders the message straight to a YAML node. medkit's JsonSerializer::to_json builds this
  // same node internally and then converts it to JSON, so routing through it would round-trip
  // message -> YAML -> JSON -> YAML. Calling dynmsg directly keeps it message -> YAML -> string.
  const RosMessage_Cpp ros_msg{&members, const_cast<uint8_t*>(static_cast<const uint8_t*>(message))};
  return dynmsg::yaml_to_string(dynmsg::cpp::message_to_yaml(ros_msg));
}

std::optional<rclcpp::SerializedMessage> serializedMessageFromYaml(const std::string& msg_type,
                                                                   const std::string& payload, std::string& error) {
  const auto root = loadPayload(payload, error);
  if (!root) {
    return std::nullopt;
  }

  try {
    static const ros2_medkit_serialization::JsonSerializer serializer;
    return serializer.serialize(msg_type, ros2_medkit_serialization::JsonSerializer::yaml_to_json(*root));
  } catch (const std::exception& resolve_error) {
    error = resolve_error.what();
    return std::nullopt;
  }
}

std::optional<rclcpp::SerializedMessage> serializedMessageFromJson(const std::string& msg_type,
                                                                   const std::string& payload, std::string& error) {
  error.clear();
  if (payload.empty()) {
    error = "payload must be non-empty";
    return std::nullopt;
  }
  if (payload.size() > cli::kMaxYamlPayloadBytes) {
    error = "payload exceeds maximum size of " + std::to_string(cli::kMaxYamlPayloadBytes) + " bytes";
    return std::nullopt;
  }

  nlohmann::json root;
  try {
    root = nlohmann::json::parse(payload);
  } catch (const nlohmann::json::exception& parse_error) {
    error = std::string("payload is not valid JSON: ") + parse_error.what();
    return std::nullopt;
  }
  if (!root.is_object()) {
    error = "payload must be a JSON object";
    return std::nullopt;
  }
  if (containsOversizedSequence(root)) {
    error = "payload contains an array exceeding maximum length " + std::to_string(cli::kMaxResizableSequenceLength);
    return std::nullopt;
  }

  try {
    static const ros2_medkit_serialization::JsonSerializer serializer;
    return serializer.serialize(msg_type, root);
  } catch (const std::exception& conversion_error) {
    error = conversion_error.what();
    return std::nullopt;
  }
}

std::optional<std::string> jsonFromSerializedMessage(const std::string& msg_type,
                                                     const rclcpp::SerializedMessage& serialized_msg,
                                                     std::string& error) {
  error.clear();
  try {
    static const ros2_medkit_serialization::JsonSerializer serializer;
    return serializer.deserialize(msg_type, serialized_msg).dump();
  } catch (const std::exception& conversion_error) {
    error = conversion_error.what();
    return std::nullopt;
  }
}

std::optional<std::string> renderJsonSchema(const std::string& msg_type, std::string& error) {
  error.clear();
  try {
    static const ros2_medkit_serialization::JsonSerializer serializer;
    return serializer.get_schema(msg_type).dump();
  } catch (const std::exception& schema_error) {
    error = schema_error.what();
    return std::nullopt;
  }
}

bool populateMessageFromYaml(const std::string& msg_type, const std::string& payload, void* message,
                             std::string& error) {
  if (message == nullptr) {
    error = "message storage must be non-null";
    return false;
  }

  const auto root = loadPayload(payload, error);
  if (!root) {
    return false;
  }

  try {
    const auto* type_info = ros2_medkit_serialization::TypeCache::instance().get_message_type_info(msg_type);
    if (type_info == nullptr) {
      error = "Type not found: " + msg_type;
      return false;
    }
    static const ros2_medkit_serialization::JsonSerializer serializer;
    serializer.from_json_to_message(type_info, ros2_medkit_serialization::JsonSerializer::yaml_to_json(*root), message);
    return true;
  } catch (const std::exception& conversion_error) {
    error = conversion_error.what();
    return false;
  }
}

} // namespace ros_portal::introspection
