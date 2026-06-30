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

#include "ros2_livekit_bridge/ros2_cli/yaml_message_converter.hpp"

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <rclcpp/serialization.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_typesupport_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <string>
#include <type_traits>
#include <utility>

#include "ros2_livekit_bridge/ros2_cli/constants.hpp"
#include "ros2_livekit_bridge/ros2_cli/dynamic_message.hpp"

namespace ros2_livekit_bridge::ros2_cli {
namespace {

namespace introspection = rosidl_typesupport_introspection_cpp;
using introspection::MessageMember;
using introspection::MessageMembers;

const MessageMembers *messageMembersFromTypeSupport(const rosidl_message_type_support_t *type_support,
                                                    const std::string &msg_type, std::string &error) {
  if (type_support == nullptr || type_support->data == nullptr) {
    error = "type support for '" + msg_type + "' does not contain introspection data";
    return nullptr;
  }
  return static_cast<const MessageMembers *>(type_support->data);
}

const MessageMembers *nestedMessageMembers(const MessageMember &member, const std::string &path, std::string &error) {
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    error = "field '" + path + "' does not contain nested message metadata";
    return nullptr;
  }
  return static_cast<const MessageMembers *>(member.members_->data);
}

void *memberMemory(void *message, const MessageMember &member) {
  return static_cast<void *>(static_cast<std::uint8_t *>(message) + member.offset_);
}

const MessageMember *findMember(const MessageMembers &members, const std::string &field_name) {
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto &member = members.members_[index];
    if (field_name == member.name_) {
      return &member;
    }
  }
  return nullptr;
}

template <typename T>
bool assignScalarValue(const MessageMember &member, const YAML::Node &node, void *destination, const std::string &path,
                       std::string &error) {
  (void)member;
  if constexpr (std::is_same_v<T, bool>) {
    try {
      *static_cast<T *>(destination) = node.as<bool>();
    } catch (const std::exception &parse_error) {
      error = "field '" + path + "' must be a boolean: " + parse_error.what();
      return false;
    }
    return true;
  } else if constexpr (std::is_floating_point_v<T>) {
    const auto value = detail::checkedFloat<T>(node, path, error);
    if (!value) {
      return false;
    }
    *static_cast<T *>(destination) = *value;
    return true;
  } else {
    const auto value = detail::checkedInteger<T>(node, path, error);
    if (!value) {
      return false;
    }
    *static_cast<T *>(destination) = *value;
    return true;
  }
}

template <>
bool assignScalarValue<std::string>(const MessageMember &member, const YAML::Node &node, void *destination,
                                    const std::string &path, std::string &error) {
  auto value = detail::checkedString(node, member.string_upper_bound_, path, error);
  if (!value) {
    return false;
  }
  *static_cast<std::string *>(destination) = std::move(*value);
  return true;
}

template <>
bool assignScalarValue<std::u16string>(const MessageMember &member, const YAML::Node &node, void *destination,
                                       const std::string &path, std::string &error) {
  auto value = detail::checkedU16String(node, member.string_upper_bound_, path, error);
  if (!value) {
    return false;
  }
  *static_cast<std::u16string *>(destination) = std::move(*value);
  return true;
}

template <>
bool assignScalarValue<char>(const MessageMember &member, const YAML::Node &node, void *destination,
                             const std::string &path, std::string &error) {
  (void)member;
  const auto value = detail::checkedChar(node, path, error);
  if (!value) {
    return false;
  }
  *static_cast<char *>(destination) = *value;
  return true;
}

template <>
bool assignScalarValue<char16_t>(const MessageMember &member, const YAML::Node &node, void *destination,
                                 const std::string &path, std::string &error) {
  (void)member;
  const auto value = detail::checkedWChar(node, path, error);
  if (!value) {
    return false;
  }
  *static_cast<char16_t *>(destination) = *value;
  return true;
}

template <typename T>
bool assignArrayScalarValue(const MessageMember &member, const YAML::Node &node, void *sequence, std::size_t index,
                            const std::string &path, std::string &error) {
  T value{};
  if (!assignScalarValue<T>(member, node, &value, path, error)) {
    return false;
  }
  if (member.assign_function != nullptr) {
    member.assign_function(sequence, index, &value);
    return true;
  }
  auto *item = member.get_function(sequence, index);
  *static_cast<T *>(item) = value;
  return true;
}

bool assignField(const MessageMember &member, const YAML::Node &node, void *field_memory, const std::string &path,
                 std::string &error);

bool assignMessage(const YAML::Node &node, void *message, const MessageMembers &members, const std::string &path,
                   std::string &error) {
  if (!node.IsMap()) {
    error = "field '" + path + "' must be a YAML map";
    return false;
  }

  for (const auto &entry : node) {
    const auto field_name = entry.first.as<std::string>();
    const auto *member = findMember(members, field_name);
    if (member == nullptr) {
      error = "field '" + path + "." + field_name + "' is not defined by message type '" + members.message_namespace_ +
              "::" + members.message_name_ + "'";
      return false;
    }
    if (!assignField(*member, entry.second, memberMemory(message, *member), path + "." + field_name, error)) {
      return false;
    }
  }
  return true;
}

bool isResizableArray(const MessageMember &member) { return member.resize_function != nullptr; }

bool validateAndResizeArray(const MessageMember &member, const YAML::Node &node, void *field_memory,
                            const std::string &path, std::string &error) {
  if (!node.IsSequence()) {
    error = "field '" + path + "' must be a YAML sequence";
    return false;
  }

  const auto requested_size = node.size();
  if (!isResizableArray(member)) {
    if (requested_size != member.array_size_) {
      error = "field '" + path + "' must contain exactly " + std::to_string(member.array_size_) + " entries";
      return false;
    }
    return true;
  }

  if (member.is_upper_bound_ && requested_size > member.array_size_) {
    error = "field '" + path + "' exceeds sequence upper bound " + std::to_string(member.array_size_);
    return false;
  }
  if (requested_size > kMaxResizableSequenceLength) {
    error = "field '" + path + "' exceeds maximum sequence length " + std::to_string(kMaxResizableSequenceLength);
    return false;
  }
  member.resize_function(field_memory, requested_size);
  return true;
}

bool assignArray(const MessageMember &member, const YAML::Node &node, void *field_memory, const std::string &path,
                 std::string &error) {
  if (!validateAndResizeArray(member, node, field_memory, path, error)) {
    return false;
  }
  for (std::size_t index = 0; index < node.size(); ++index) {
    const auto element_path = path + "[" + std::to_string(index) + "]";
    if (member.type_id_ == introspection::ROS_TYPE_MESSAGE) {
      auto *item = member.get_function(field_memory, index);
      const auto *nested = nestedMessageMembers(member, element_path, error);
      if (nested == nullptr) {
        return false;
      }
      if (!assignMessage(node[index], item, *nested, element_path, error)) {
        return false;
      }
      continue;
    }

    bool ok = true;
    switch (member.type_id_) {
      case introspection::ROS_TYPE_FLOAT:
        ok = assignArrayScalarValue<float>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_DOUBLE:
        ok = assignArrayScalarValue<double>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_LONG_DOUBLE:
        ok = assignArrayScalarValue<long double>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_CHAR:
        ok = assignArrayScalarValue<char>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_WCHAR:
        ok = assignArrayScalarValue<char16_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_BOOLEAN:
        ok = assignArrayScalarValue<bool>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_OCTET:
      case introspection::ROS_TYPE_UINT8:
        ok = assignArrayScalarValue<std::uint8_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_INT8:
        ok = assignArrayScalarValue<std::int8_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_UINT16:
        ok = assignArrayScalarValue<std::uint16_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_INT16:
        ok = assignArrayScalarValue<std::int16_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_UINT32:
        ok = assignArrayScalarValue<std::uint32_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_INT32:
        ok = assignArrayScalarValue<std::int32_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_UINT64:
        ok = assignArrayScalarValue<std::uint64_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_INT64:
        ok = assignArrayScalarValue<std::int64_t>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_STRING:
        ok = assignArrayScalarValue<std::string>(member, node[index], field_memory, index, element_path, error);
        break;
      case introspection::ROS_TYPE_WSTRING:
        ok = assignArrayScalarValue<std::u16string>(member, node[index], field_memory, index, element_path, error);
        break;
      default:
        error = "field '" + element_path + "' has unsupported ROS type id " + std::to_string(member.type_id_);
        return false;
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool assignField(const MessageMember &member, const YAML::Node &node, void *field_memory, const std::string &path,
                 std::string &error) {
  if (member.is_array_) {
    return assignArray(member, node, field_memory, path, error);
  }

  switch (member.type_id_) {
    case introspection::ROS_TYPE_FLOAT:
      return assignScalarValue<float>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_DOUBLE:
      return assignScalarValue<double>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_LONG_DOUBLE:
      return assignScalarValue<long double>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_CHAR:
      return assignScalarValue<char>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_WCHAR:
      return assignScalarValue<char16_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_BOOLEAN:
      return assignScalarValue<bool>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_OCTET:
    case introspection::ROS_TYPE_UINT8:
      return assignScalarValue<std::uint8_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_INT8:
      return assignScalarValue<std::int8_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_UINT16:
      return assignScalarValue<std::uint16_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_INT16:
      return assignScalarValue<std::int16_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_UINT32:
      return assignScalarValue<std::uint32_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_INT32:
      return assignScalarValue<std::int32_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_UINT64:
      return assignScalarValue<std::uint64_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_INT64:
      return assignScalarValue<std::int64_t>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_STRING:
      return assignScalarValue<std::string>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_WSTRING:
      return assignScalarValue<std::u16string>(member, node, field_memory, path, error);
    case introspection::ROS_TYPE_MESSAGE: {
      const auto *nested = nestedMessageMembers(member, path, error);
      if (nested == nullptr) {
        return false;
      }
      return assignMessage(node, field_memory, *nested, path, error);
    }
    default:
      error = "field '" + path + "' has unsupported ROS type id " + std::to_string(member.type_id_);
      return false;
  }
}

} // namespace

std::optional<rclcpp::SerializedMessage> serializedMessageFromYaml(const std::string &msg_type,
                                                                   const std::string &payload, std::string &error) {
  if (payload.empty()) {
    error = "payload must be non-empty";
    return std::nullopt;
  }
  if (payload.size() > kMaxYamlPayloadBytes) {
    error = "payload exceeds maximum size of " + std::to_string(kMaxYamlPayloadBytes) + " bytes";
    return std::nullopt;
  }

  YAML::Node root;
  try {
    root = YAML::Load(payload);
  } catch (const YAML::Exception &parse_error) {
    error = std::string("payload is not valid YAML: ") + parse_error.what();
    return std::nullopt;
  }

  // The ROS type-support lookup and CDR serialization throw for unknown types
  // and internal failures; contain those so this function never propagates.
  try {
    auto introspection_library = rclcpp::get_typesupport_library(msg_type, introspection::typesupport_identifier);
    const auto *introspection_type_support =
        rclcpp::get_message_typesupport_handle(msg_type, introspection::typesupport_identifier, *introspection_library);
    const auto *members = messageMembersFromTypeSupport(introspection_type_support, msg_type, error);
    if (members == nullptr) {
      return std::nullopt;
    }

    DynamicMessage message(*members);
    if (!assignMessage(root, message.data(), *members, msg_type, error)) {
      return std::nullopt;
    }

    auto serialization_library =
        rclcpp::get_typesupport_library(msg_type, rosidl_typesupport_cpp::typesupport_identifier);
    const auto *serialization_type_support = rclcpp::get_message_typesupport_handle(
        msg_type, rosidl_typesupport_cpp::typesupport_identifier, *serialization_library);

    rclcpp::SerializationBase serialization(serialization_type_support);
    rclcpp::SerializedMessage serialized;
    serialization.serialize_message(message.data(), &serialized);
    return serialized;
  } catch (const std::exception &resolve_error) {
    error = resolve_error.what();
    return std::nullopt;
  }
}

} // namespace ros2_livekit_bridge::ros2_cli
