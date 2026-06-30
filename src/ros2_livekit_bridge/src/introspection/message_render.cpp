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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::message_render
{

using introspection::MessageMember;
using introspection::MessageMembers;

namespace
{

// The ROS 2 introspection C API exposes nested-message metadata as a
// type-erased `void *` on the type-support handle, so this cast is unavoidable.
// Keeping it in a single checked helper means the rest of the renderer never
// touches the raw pointer and there is exactly one place to audit.
const MessageMembers * nestedMembers(const MessageMember & member)
{
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    return nullptr;
  }
  return static_cast<const MessageMembers *>(member.members_->data);
}

}  // namespace

bool isYamlKeyword(const std::string & value)
{
  std::string lower;
  lower.reserve(value.size());
  std::transform(
      value.begin(), value.end(), std::back_inserter(lower),
    [](unsigned char ch) {return static_cast<char>(std::tolower(ch));});
  return lower == "true" || lower == "false" || lower == "null" ||
         lower == "~" || lower == "yes" || lower == "no" || lower == "on" ||
         lower == "off";
}

bool canRenderPlainString(const std::string & value)
{
  if (value.empty() || isYamlKeyword(value)) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
             return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/';
  });
}

void renderQuotedString(std::ostringstream & stream, const std::string & value)
{
  stream << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (std::isprint(ch)) {
          stream << static_cast<char>(ch);
        } else {
          stream << "\\x" << std::uppercase << std::hex << std::setw(2)
                 << std::setfill('0') << static_cast<int>(ch) << std::nouppercase
                 << std::dec << std::setfill(' ');
        }
        break;
    }
  }
  stream << '"';
}

void renderString(std::ostringstream & stream, const std::string & value)
{
  if (canRenderPlainString(value)) {
    stream << value;
    return;
  }
  renderQuotedString(stream, value);
}

bool isNestedMessageBlock(const MessageMember & member)
{
  return !member.is_array_ &&
         member.type_id_ == introspection::ROS_TYPE_MESSAGE &&
         nestedMembers(member) != nullptr;
}

void renderMessageArrayItem(
  std::ostringstream & stream,
  const MessageMembers & members, const void *message,
  std::size_t indent)
{
  const std::string item_padding(indent + 2U, ' ');
  const std::string field_padding(indent + 4U, ' ');
  if (members.member_count_ == 0U) {
    stream << item_padding << "- {}";
    return;
  }

  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto & member = members.members_[index];
    stream << (index == 0U ? item_padding + "- " : field_padding)
           << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member), indent + 4U);
    if (!isNestedMessageBlock(member) && index + 1U < members.member_count_) {
      stream << '\n';
    }
  }
}

const void * memberMemory(const void *message, const MessageMember & member)
{
  return static_cast<const void *>(static_cast<const std::uint8_t *>(message) +
         member.offset_);
}

void renderMessage(
  std::ostringstream & stream, const MessageMembers & members,
  const void *message, std::size_t indent)
{
  const std::string padding(indent, ' ');
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto & member = members.members_[index];
    stream << padding << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member), indent);
    if (!isNestedMessageBlock(member)) {
      stream << '\n';
    }
  }
}

void renderNestedMessage(
  std::ostringstream & stream,
  const MessageMember & member, const void *field_memory,
  std::size_t indent)
{
  const MessageMembers * members = nestedMembers(member);
  if (members == nullptr) {
    stream << "{}";
    return;
  }
  stream << '\n';
  renderMessage(stream, *members, field_memory, indent + 2U);
}

void renderSingleField(
  std::ostringstream & stream, const MessageMember & member,
  const void *field_memory, std::size_t indent)
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
      stream << static_cast<unsigned>(
        *static_cast<const unsigned char *>(field_memory));
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
      stream << static_cast<int>(*static_cast<const std::int8_t *>(field_memory));
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
      renderString(stream, *static_cast<const std::string *>(field_memory));
      break;
    case introspection::ROS_TYPE_WSTRING: {
        std::string value;
        const auto & wide_value = *static_cast<const std::u16string *>(field_memory);
        value.reserve(wide_value.size());
        for (const auto code_unit : wide_value) {
          value.push_back(static_cast<char>(code_unit));
        }
        renderString(stream, value);
      } break;
    case introspection::ROS_TYPE_MESSAGE:
      renderNestedMessage(stream, member, field_memory, indent);
      break;
    default:
      stream << "<unsupported ROS type id " << member.type_id_ << ">";
      break;
  }
}

void renderArrayField(
  std::ostringstream & stream, const MessageMember & member,
  const void *field_memory, std::size_t indent)
{
  const auto size = member.size_function == nullptr ?
    member.array_size_ :
    member.size_function(field_memory);
  if (member.type_id_ == introspection::ROS_TYPE_MESSAGE) {
    const MessageMembers * nested = nestedMembers(member);
    if (size == 0U || nested == nullptr) {
      stream << "[]";
      return;
    }
    stream << '\n';
    const auto & members = *nested;
    for (std::size_t index = 0; index < size; ++index) {
      if (index > 0U) {
        stream << '\n';
      }
      const auto *item =
        member.get_function(const_cast<void *>(field_memory), index);
      renderMessageArrayItem(stream, members, item, indent);
    }
    return;
  }

  stream << '[';
  for (std::size_t index = 0; index < size; ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    const auto *item =
      member.get_function(const_cast<void *>(field_memory), index);
    renderSingleField(stream, member, item, indent);
  }
  stream << ']';
}

void renderField(
  std::ostringstream & stream, const MessageMember & member,
  const void *field_memory, std::size_t indent)
{
  if (member.is_array_) {
    renderArrayField(stream, member, field_memory, indent);
    return;
  }
  renderSingleField(stream, member, field_memory, indent);
}

std::string toYaml(const MessageMembers & members, const void *message)
{
  std::ostringstream stream;
  renderMessage(stream, members, message);
  return stream.str();
}

} // namespace ros2_livekit_bridge::message_render
