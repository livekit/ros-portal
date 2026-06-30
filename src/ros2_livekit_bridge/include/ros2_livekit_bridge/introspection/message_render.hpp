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

/// @brief Render introspection-backed runtime ROS messages as CLI-style YAML.
///
/// These helpers walk message introspection metadata to format an arbitrary
/// runtime message the way the ROS 2 command-line tools print it. They are not
/// tied to any single command; @ref toYaml is the public entry point and the
/// remaining helpers are implementation details exposed only so they can be
/// unit tested directly.

#pragma once

#include <cstddef>
#include <sstream>
#include <string>

#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace ros2_livekit_bridge::message_render
{

/// @brief Introspection type-support namespace alias.
namespace introspection = rosidl_typesupport_introspection_cpp;

/// @brief Render a scalar field value as YAML-ish CLI text.
/// @tparam T Concrete scalar type stored at @p data.
/// @param stream Destination stream for the rendered value.
/// @param data Pointer to the scalar field memory.
template<typename T>
void renderScalar(std::ostringstream & stream, const void *data)
{
  stream << *static_cast<const T *>(data);
}

/// @brief Return whether @p value is a YAML keyword that needs quoting.
/// @param value String value to inspect.
/// @return True when @p value is a YAML boolean/null-style keyword.
bool isYamlKeyword(const std::string & value);

/// @brief Return whether @p value can be rendered as an unquoted YAML scalar.
/// @param value String value to inspect.
/// @return True when @p value is safe to render without quotes.
bool canRenderPlainString(const std::string & value);

/// @brief Render a double-quoted YAML string scalar with escapes.
/// @param stream Destination stream for the rendered value.
/// @param value String value to quote.
void renderQuotedString(std::ostringstream & stream, const std::string & value);

/// @brief Render a string as plain YAML when safe, otherwise quoted.
/// @param stream Destination stream for the rendered value.
/// @param value String value to render.
void renderString(std::ostringstream & stream, const std::string & value);

/// @brief Render a UTF-16 wide string as CLI-style YAML.
/// @param stream Destination stream for the rendered value.
/// @param value UTF-16 string value to render.
///
/// ASCII-only values reuse @ref renderString. Any non-ASCII code point is
/// emitted as a \uXXXX (BMP) or \U00XXXXXX (astral) escape so the output is
/// lossless and locale-independent. Surrogate pairs are decoded to a single
/// code point.
void renderWideString(std::ostringstream & stream, const std::u16string & value);

/// @brief Return a pointer to a member inside a message buffer.
/// @param message Pointer to the start of the message memory.
/// @param member Introspection metadata describing the member offset.
/// @return Pointer to @p member inside @p message.
const void * memberMemory(
  const void *message,
  const introspection::MessageMember & member);

/// @brief Return whether a member renders as a nested message block.
/// @param member Introspection metadata for the field.
/// @return True when @p member is a non-array message with metadata.
bool isNestedMessageBlock(const introspection::MessageMember & member);

/// @brief Render one message as CLI-style YAML.
/// @param stream Destination stream for the rendered message.
/// @param members Introspection metadata for the message type.
/// @param message Pointer to the runtime message memory.
/// @param indent Current indentation depth in spaces.
void renderMessage(
  std::ostringstream & stream,
  const introspection::MessageMembers & members,
  const void *message, std::size_t indent = 0U);

/// @brief Render a nested message-typed field.
/// @param stream Destination stream for the rendered value.
/// @param member Introspection metadata for the nested field.
/// @param field_memory Pointer to the nested field memory.
/// @param indent Current indentation depth in spaces.
void renderNestedMessage(
  std::ostringstream & stream,
  const introspection::MessageMember & member,
  const void *field_memory, std::size_t indent);

/// @brief Render one element of an array-of-messages YAML block sequence.
/// @param stream Destination stream for the rendered item.
/// @param members Introspection metadata for the array element message type.
/// @param message Pointer to the array element message memory.
/// @param indent Parent field indentation depth in spaces.
void renderMessageArrayItem(
  std::ostringstream & stream,
  const introspection::MessageMembers & members,
  const void *message, std::size_t indent);

/// @brief Render one non-array scalar or nested field.
/// @param stream Destination stream for the rendered value.
/// @param member Introspection metadata for the field.
/// @param field_memory Pointer to the field memory.
/// @param indent Current indentation depth in spaces.
void renderSingleField(
  std::ostringstream & stream,
  const introspection::MessageMember & member,
  const void *field_memory, std::size_t indent);

/// @brief Render one array field as a YAML sequence.
/// @param stream Destination stream for the rendered value.
/// @param member Introspection metadata for the array field.
/// @param field_memory Pointer to the array field memory.
/// @param indent Current indentation depth in spaces.
void renderArrayField(
  std::ostringstream & stream,
  const introspection::MessageMember & member,
  const void *field_memory, std::size_t indent);

/// @brief Render one introspection field value, scalar or array.
/// @param stream Destination stream for the rendered value.
/// @param member Introspection metadata for the field.
/// @param field_memory Pointer to the field memory.
/// @param indent Current indentation depth in spaces.
void renderField(
  std::ostringstream & stream,
  const introspection::MessageMember & member,
  const void *field_memory, std::size_t indent);

/// @brief Format a runtime message as CLI-style YAML.
/// @param members Introspection metadata for the message type.
/// @param message Pointer to the runtime message memory.
/// @return CLI-style YAML rendering of @p message.
std::string toYaml(
  const introspection::MessageMembers & members,
  const void *message);

} // namespace ros2_livekit_bridge::message_render
