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

#include "ros2_livekit_bridge/introspection/message_render.hpp"

#include <cstdint>
#include <sstream>
#include <string>

#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::message_render
{

using introspection::MessageMember;
using introspection::MessageMembers;

const void * memberMemory(const void * message, const MessageMember & member)
{
  return static_cast<const void *>(
    static_cast<const std::uint8_t *>(message) + member.offset_);
}

void renderMessage(
  std::ostringstream & stream,
  const MessageMembers & members,
  const void * message,
  std::size_t indent)
{
  const std::string padding(indent, ' ');
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto & member = members.members_[index];
    stream << padding << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member), indent);
    stream << '\n';
  }
}

void renderNestedMessage(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory,
  std::size_t indent)
{
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    stream << "{}";
    return;
  }
  stream << '\n';
  renderMessage(
    stream, *static_cast<const MessageMembers *>(member.members_->data),
    field_memory, indent + 2U);
}

void renderSingleField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory,
  std::size_t indent)
{
  switch (member.type_id_) {
    case introspection::ROS_TYPE_FLOAT:
      renderScalar<float>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_DOUBLE:
      renderScalar<double>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_LONG_DOUBLE:
      renderScalar<long double>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_CHAR:
      stream << *static_cast<const char *>(field_memory);
      break;
    case introspection::ROS_TYPE_WCHAR:
      stream << static_cast<std::uint32_t>(
        *static_cast<const char16_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_BOOLEAN:
      stream << (*static_cast<const bool *>(field_memory) ? "true" : "false");
      break;
    case introspection::ROS_TYPE_OCTET:
    case introspection::ROS_TYPE_UINT8:
      stream << static_cast<unsigned>(
        *static_cast<const std::uint8_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_INT8:
      stream << static_cast<int>(
        *static_cast<const std::int8_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_UINT16:
      renderScalar<std::uint16_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT16:
      renderScalar<std::int16_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_UINT32:
      renderScalar<std::uint32_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT32:
      renderScalar<std::int32_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_UINT64:
      renderScalar<std::uint64_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_INT64:
      renderScalar<std::int64_t>(stream, field_memory);
      break;
    case introspection::ROS_TYPE_STRING:
      stream << *static_cast<const std::string *>(field_memory);
      break;
    case introspection::ROS_TYPE_WSTRING:
      for (const auto code_unit :
        *static_cast<const std::u16string *>(field_memory))
      {
        stream << static_cast<char>(code_unit);
      }
      break;
    case introspection::ROS_TYPE_MESSAGE:
      renderNestedMessage(stream, member, field_memory, indent);
      break;
    default:
      stream << "<unsupported ROS type id " << member.type_id_ << ">";
      break;
  }
}

void renderArrayField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory,
  std::size_t indent)
{
  const auto size = member.size_function == nullptr ?
    member.array_size_ : member.size_function(field_memory);
  stream << '[';
  for (std::size_t index = 0; index < size; ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    const auto * item = member.get_function(
      const_cast<void *>(field_memory), index);
    renderSingleField(stream, member, item, indent);
  }
  stream << ']';
}

void renderField(
  std::ostringstream & stream,
  const MessageMember & member,
  const void * field_memory,
  std::size_t indent)
{
  if (member.is_array_) {
    renderArrayField(stream, member, field_memory, indent);
    return;
  }
  renderSingleField(stream, member, field_memory, indent);
}

std::string toYaml(
  const MessageMembers & members,
  const void * message)
{
  std::ostringstream stream;
  renderMessage(stream, members, message);
  return stream.str();
}

}  // namespace ros2_livekit_bridge::message_render
