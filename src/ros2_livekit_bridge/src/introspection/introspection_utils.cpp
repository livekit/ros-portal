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

#include "ros2_livekit_bridge/introspection/introspection_utils.hpp"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <optional>
#include <ros2_medkit_serialization/json_serializer.hpp>
#include <ros2_medkit_serialization/type_cache.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"

namespace ros2_livekit_bridge::introspection {

bool containsOversizedSequence(const YAML::Node &node) {
  if (node.IsSequence()) {
    if (node.size() > ros2_cli::kMaxResizableSequenceLength) {
      return true;
    }
    for (const auto &entry : node) {
      if (containsOversizedSequence(entry)) {
        return true;
      }
    }
    return false;
  }

  if (node.IsMap()) {
    for (const auto &entry : node) {
      if (containsOversizedSequence(entry.second)) {
        return true;
      }
    }
  }
  return false;
}

std::optional<YAML::Node> loadPayload(const std::string &payload, std::string &error) {
  if (payload.empty()) {
    error = "payload must be non-empty";
    return std::nullopt;
  }
  if (payload.size() > ros2_cli::kMaxYamlPayloadBytes) {
    error = "payload exceeds maximum size of " + std::to_string(ros2_cli::kMaxYamlPayloadBytes) + " bytes";
    return std::nullopt;
  }

  try {
    auto root = YAML::Load(payload);
    if (containsOversizedSequence(root)) {
      error = "payload contains a sequence exceeding maximum length " +
              std::to_string(ros2_cli::kMaxResizableSequenceLength);
      return std::nullopt;
    }
    return root;
  } catch (const YAML::Exception &parse_error) {
    error = std::string("payload is not valid YAML: ") + parse_error.what();
    return std::nullopt;
  }
}

std::optional<std::string> messageTypeString(const rosidl_typesupport_introspection_cpp::MessageMembers &members) {
  std::string message_namespace = members.message_namespace_ == nullptr ? "" : members.message_namespace_;
  const std::string message_name = members.message_name_ == nullptr ? "" : members.message_name_;
  if (message_namespace.empty() || message_name.empty()) {
    return std::nullopt;
  }

  std::size_t position = 0U;
  while ((position = message_namespace.find("::", position)) != std::string::npos) {
    message_namespace.replace(position, 2U, "/");
    ++position;
  }
  return message_namespace + "/" + message_name;
}

std::string toYaml(const std::string &msg_type, const void *message) {
  ros2_medkit_serialization::JsonSerializer serializer;
  const auto json = serializer.to_json(msg_type, message);
  const auto yaml = ros2_medkit_serialization::JsonSerializer::json_to_yaml(json);
  std::ostringstream stream;
  stream << yaml;
  return stream.str();
}

std::string toYaml(const rosidl_typesupport_introspection_cpp::MessageMembers &members, const void *message) {
  const auto msg_type = messageTypeString(members);
  if (!msg_type.has_value()) {
    throw std::runtime_error("message introspection metadata is missing namespace or name");
  }
  return toYaml(*msg_type, message);
}

std::optional<rclcpp::SerializedMessage> serializedMessageFromYaml(const std::string &msg_type,
                                                                   const std::string &payload, std::string &error) {
  const auto root = loadPayload(payload, error);
  if (!root) {
    return std::nullopt;
  }

  try {
    ros2_medkit_serialization::JsonSerializer serializer;
    return serializer.serialize(msg_type, ros2_medkit_serialization::JsonSerializer::yaml_to_json(*root));
  } catch (const std::exception &resolve_error) {
    error = resolve_error.what();
    return std::nullopt;
  }
}

bool populateMessageFromYaml(const std::string &msg_type, const std::string &payload, void *message,
                             std::string &error) {
  if (message == nullptr) {
    error = "message storage must be non-null";
    return false;
  }

  const auto root = loadPayload(payload, error);
  if (!root) {
    return false;
  }

  try {
    const auto *type_info = ros2_medkit_serialization::TypeCache::instance().get_message_type_info(msg_type);
    if (type_info == nullptr) {
      error = "Type not found: " + msg_type;
      return false;
    }
    ros2_medkit_serialization::JsonSerializer serializer;
    serializer.from_json_to_message(type_info, ros2_medkit_serialization::JsonSerializer::yaml_to_json(*root), message);
    return true;
  } catch (const std::exception &conversion_error) {
    error = conversion_error.what();
    return false;
  }
}

} // namespace ros2_livekit_bridge::introspection
