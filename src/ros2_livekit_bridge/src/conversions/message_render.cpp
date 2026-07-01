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

#include "ros2_livekit_bridge/conversions/message_render.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <sstream>
#include <string>

namespace ros2_livekit_bridge::message_render {

using introspection::MessageMember;
using introspection::MessageMembers;

namespace {

// The ROS 2 introspection C API exposes nested-message metadata as a
// type-erased `void *` on the type-support handle, so this cast is unavoidable.
// Keeping it in a single checked helper means the rest of the renderer never
// touches the raw pointer and there is exactly one place to audit.
const MessageMembers *nestedMembers(const MessageMember &member) {
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    return nullptr;
  }
  return static_cast<const MessageMembers *>(member.members_->data);
}

// Decode UTF-16 (including surrogate pairs) into Unicode code points. A lone or
// invalid surrogate is passed through unchanged so rendering stays lossless.
std::u32string decodeUtf16(const std::u16string &value) {
  std::u32string code_points;
  code_points.reserve(value.size());
  for (std::size_t index = 0U; index < value.size(); ++index) {
    const char32_t unit = value[index];
    if (unit >= 0xD800U && unit <= 0xDBFFU && index + 1U < value.size()) {
      const char32_t low = value[index + 1U];
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        code_points.push_back(0x10000U + ((unit - 0xD800U) << 10U) + (low - 0xDC00U));
        ++index;
        continue;
      }
    }
    code_points.push_back(unit);
  }
  return code_points;
}

// Append a single ASCII byte to a double-quoted YAML scalar, applying the
// standard escapes (\\, \", \n, \r, \t) and \xNN for non-printable bytes.
void appendEscapedAsciiByte(std::ostringstream &stream, unsigned char ch) {
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
        stream << "\\x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch)
               << std::nouppercase << std::dec << std::setfill(' ');
      }
      break;
  }
}

} // namespace

bool isYamlKeyword(const std::string &value) {
  std::string lower;
  lower.reserve(value.size());
  std::transform(value.begin(), value.end(), std::back_inserter(lower),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lower == "true" || lower == "false" || lower == "null" || lower == "~" || lower == "yes" || lower == "no" ||
         lower == "on" || lower == "off";
}

bool canRenderPlainString(const std::string &value) {
  if (value.empty() || isYamlKeyword(value)) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/';
  });
}

void renderQuotedString(std::ostringstream &stream, const std::string &value) {
  stream << '"';
  for (const unsigned char ch : value) {
    appendEscapedAsciiByte(stream, ch);
  }
  stream << '"';
}

void renderString(std::ostringstream &stream, const std::string &value) {
  if (canRenderPlainString(value)) {
    stream << value;
    return;
  }
  renderQuotedString(stream, value);
}

void renderWideString(std::ostringstream &stream, const std::u16string &value) {
  const std::u32string code_points = decodeUtf16(value);
  const bool is_ascii =
      std::all_of(code_points.begin(), code_points.end(), [](char32_t code_point) { return code_point <= 0x7FU; });
  if (is_ascii) {
    renderString(stream, std::string(code_points.begin(), code_points.end()));
    return;
  }
  stream << '"';
  for (const char32_t code_point : code_points) {
    if (code_point <= 0x7FU) {
      appendEscapedAsciiByte(stream, static_cast<unsigned char>(code_point));
    } else {
      const int width = code_point <= 0xFFFFU ? 4 : 8;
      stream << (code_point <= 0xFFFFU ? "\\u" : "\\U") << std::uppercase << std::hex << std::setw(width)
             << std::setfill('0') << static_cast<std::uint32_t>(code_point) << std::nouppercase << std::dec
             << std::setfill(' ');
    }
  }
  stream << '"';
}

bool isNestedMessageBlock(const MessageMember &member) {
  return !member.is_array_ && member.type_id_ == introspection::ROS_TYPE_MESSAGE && nestedMembers(member) != nullptr;
}

void renderMessageArrayItem(std::ostringstream &stream, const MessageMembers &members, const void *message,
                            std::size_t indent) {
  const std::string item_padding(indent + 2U, ' ');
  const std::string field_padding(indent + 4U, ' ');
  if (members.member_count_ == 0U) {
    stream << item_padding << "- {}";
    return;
  }

  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto &member = members.members_[index];
    stream << (index == 0U ? item_padding + "- " : field_padding) << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member), indent + 4U);
    if (!isNestedMessageBlock(member) && index + 1U < members.member_count_) {
      stream << '\n';
    }
  }
}

const void *memberMemory(const void *message, const MessageMember &member) {
  return static_cast<const void *>(static_cast<const std::uint8_t *>(message) + member.offset_);
}

void renderMessage(std::ostringstream &stream, const MessageMembers &members, const void *message, std::size_t indent) {
  const std::string padding(indent, ' ');
  for (std::uint32_t index = 0; index < members.member_count_; ++index) {
    const auto &member = members.members_[index];
    stream << padding << member.name_ << ": ";
    renderField(stream, member, memberMemory(message, member), indent);
    if (!isNestedMessageBlock(member)) {
      stream << '\n';
    }
  }
}

void renderNestedMessage(std::ostringstream &stream, const MessageMember &member, const void *field_memory,
                         std::size_t indent) {
  const MessageMembers *members = nestedMembers(member);
  if (members == nullptr) {
    stream << "{}";
    return;
  }
  stream << '\n';
  renderMessage(stream, *members, field_memory, indent + 2U);
}

void renderSingleField(std::ostringstream &stream, const MessageMember &member, const void *field_memory,
                       std::size_t indent) {
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
      stream << static_cast<unsigned>(*static_cast<const unsigned char *>(field_memory));
      break;
    case introspection::ROS_TYPE_WCHAR:
      stream << static_cast<std::uint32_t>(*static_cast<const char16_t *>(field_memory));
      break;
    case introspection::ROS_TYPE_BOOLEAN:
      stream << (*static_cast<const bool *>(field_memory) ? "true" : "false");
      break;
    case introspection::ROS_TYPE_OCTET:
    case introspection::ROS_TYPE_UINT8:
      stream << static_cast<unsigned>(*static_cast<const std::uint8_t *>(field_memory));
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
    case introspection::ROS_TYPE_WSTRING:
      renderWideString(stream, *static_cast<const std::u16string *>(field_memory));
      break;
    case introspection::ROS_TYPE_MESSAGE:
      renderNestedMessage(stream, member, field_memory, indent);
      break;
    default:
      stream << "<unsupported ROS type id " << member.type_id_ << ">";
      break;
  }
}

void renderArrayField(std::ostringstream &stream, const MessageMember &member, const void *field_memory,
                      std::size_t indent) {
  const auto size = member.size_function == nullptr ? member.array_size_ : member.size_function(field_memory);
  if (member.type_id_ == introspection::ROS_TYPE_MESSAGE) {
    const MessageMembers *nested = nestedMembers(member);
    if (size == 0U || nested == nullptr) {
      stream << "[]";
      return;
    }
    stream << '\n';
    const auto &members = *nested;
    for (std::size_t index = 0; index < size; ++index) {
      if (index > 0U) {
        stream << '\n';
      }
      const auto *item = member.get_function(const_cast<void *>(field_memory), index);
      renderMessageArrayItem(stream, members, item, indent);
    }
    return;
  }

  stream << '[';
  for (std::size_t index = 0; index < size; ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    const auto *item = member.get_function(const_cast<void *>(field_memory), index);
    renderSingleField(stream, member, item, indent);
  }
  stream << ']';
}

void renderField(std::ostringstream &stream, const MessageMember &member, const void *field_memory,
                 std::size_t indent) {
  if (member.is_array_) {
    renderArrayField(stream, member, field_memory, indent);
    return;
  }
  renderSingleField(stream, member, field_memory, indent);
}

std::string toYaml(const MessageMembers &members, const void *message) {
  std::ostringstream stream;
  renderMessage(stream, members, message);
  return stream.str();
}

} // namespace ros2_livekit_bridge::message_render
