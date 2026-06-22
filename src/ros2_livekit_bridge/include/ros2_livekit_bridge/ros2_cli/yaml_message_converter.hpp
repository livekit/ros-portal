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

/// @brief Parse `ros2 topic pub` YAML into serialized ROS messages.
///
/// Remote publish commands send human-readable YAML; the bridge must convert
/// that to CDR bytes before publishing. This module walks message introspection
/// to populate and serialize a dynamic message. Scalar converters in `detail`
/// are exposed for unit testing.

#pragma once

#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

#include <rclcpp/serialized_message.hpp>
#include <yaml-cpp/yaml.h>

namespace ros2_livekit_bridge::ros2_cli
{

/// @brief Convert a native `ros2 topic pub` YAML payload to serialized ROS CDR.
/// @param msg_type ROS interface type, such as `std_msgs/msg/String`.
/// @param payload YAML message payload.
/// @param error Set to a human-readable description when conversion fails.
/// @return Serialized ROS message bytes, or `std::nullopt` when the type cannot
///   be resolved or the payload is invalid.
std::optional<rclcpp::SerializedMessage>
serializedMessageFromYaml(
  const std::string & msg_type,
  const std::string & payload, std::string & error);

/// @brief Leaf YAML-to-scalar converters used while populating a ROS message.
///
/// These helpers contain the value-level parsing and range-checking logic that
/// is independent of ROS introspection. They are exposed here so they can be
/// unit tested directly against YAML nodes.
namespace detail
{

/// @brief Convert a YAML node to an integral type, enforcing its value range.
/// @tparam T Target integral type.
/// @param node YAML scalar node.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is not an integer or is out
/// of range.
/// @return The parsed value as @p T, or `std::nullopt` on failure.
template<typename T>
std::optional<T> checkedInteger(
  const YAML::Node & node, const std::string & path,
  std::string & error)
{
  static_assert(std::is_integral_v<T>, "T must be integral");
  try {
    if constexpr (std::is_signed_v<T>) {
      const auto value = node.as<long long>();
      if (value < static_cast<long long>(std::numeric_limits<T>::min()) ||
        value > static_cast<long long>(std::numeric_limits<T>::max()))
      {
        error =
          "field '" + path + "' must be an integer: integer is out of range";
        return std::nullopt;
      }
      return static_cast<T>(value);
    } else {
      const auto value = node.as<unsigned long long>();
      if (value >
        static_cast<unsigned long long>(std::numeric_limits<T>::max()))
      {
        error =
          "field '" + path + "' must be an integer: integer is out of range";
        return std::nullopt;
      }
      return static_cast<T>(value);
    }
  } catch (const std::exception & parse_error) {
    error = "field '" + path + "' must be an integer: " + parse_error.what();
    return std::nullopt;
  }
}

/// @brief Convert a YAML node to a floating point type, enforcing its
/// magnitude.
/// @tparam T Target floating point type.
/// @param node YAML scalar node.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is not numeric or is out of
/// range.
/// @return The parsed value as @p T, or `std::nullopt` on failure.
template<typename T>
std::optional<T> checkedFloat(
  const YAML::Node & node, const std::string & path,
  std::string & error)
{
  try {
    const auto value = node.as<double>();
    if (value < -static_cast<double>(std::numeric_limits<T>::max()) ||
      value > static_cast<double>(std::numeric_limits<T>::max()))
    {
      error = "field '" + path +
        "' must be numeric: floating point value is out of range";
      return std::nullopt;
    }
    return static_cast<T>(value);
  } catch (const std::exception & parse_error) {
    error = "field '" + path + "' must be numeric: " + parse_error.what();
    return std::nullopt;
  }
}

/// @brief Convert a YAML node to a string, enforcing an optional upper bound.
/// @param node YAML scalar node.
/// @param upper_bound Maximum string length, or 0 for unbounded.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is not a string or exceeds @p
/// upper_bound.
/// @return The parsed string, or `std::nullopt` on failure.
inline std::optional<std::string> checkedString(
  const YAML::Node & node,
  std::size_t upper_bound,
  const std::string & path,
  std::string & error)
{
  try {
    const auto value = node.as<std::string>();
    if (upper_bound > 0 && value.size() > upper_bound) {
      error =
        "field '" + path + "' must be a string: string exceeds upper bound";
      return std::nullopt;
    }
    return value;
  } catch (const std::exception & parse_error) {
    error = "field '" + path + "' must be a string: " + parse_error.what();
    return std::nullopt;
  }
}

/// @brief Convert a YAML node to a UTF-16 string, enforcing an optional bound.
/// @warning ASCII-only.
/// @param node YAML scalar node.
/// @param upper_bound Maximum string length, or 0 for unbounded.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is not a string or exceeds @p
/// upper_bound.
/// @return The parsed string widened code unit by code unit, or `std::nullopt`
/// on failure.
inline std::optional<std::u16string> checkedU16String(
  const YAML::Node & node,
  std::size_t upper_bound,
  const std::string & path,
  std::string & error)
{
  const auto value = checkedString(node, upper_bound, path, error);
  if (!value) {
    return std::nullopt;
  }
  std::u16string converted;
  converted.reserve(value->size());
  for (const auto ch : *value) {
    converted.push_back(static_cast<char16_t>(static_cast<unsigned char>(ch)));
  }
  return converted;
}

/// @brief Convert a YAML node to a single `char`.
/// @details A single-character scalar is taken verbatim; anything else is
/// parsed
///   as an integer code point.
/// @param node YAML scalar node.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is neither a character nor
/// integer.
/// @return The resolved character, or `std::nullopt` on failure.
inline std::optional<char> checkedChar(
  const YAML::Node & node,
  const std::string & path,
  std::string & error)
{
  try {
    const auto value = node.as<std::string>();
    if (value.size() == 1U) {
      return value.front();
    }
  } catch (const std::exception &) {
  }
  return checkedInteger<char>(node, path, error);
}

/// @brief Convert a YAML node to a single `char16_t`.
/// @details A single-character scalar is taken verbatim; anything else is
/// parsed
///   as an integer code point.
/// @param node YAML scalar node.
/// @param path Field path used in error messages.
/// @param error Set to a description when @p node is neither a character nor
/// integer.
/// @return The resolved wide character, or `std::nullopt` on failure.
inline std::optional<char16_t> checkedWChar(
  const YAML::Node & node,
  const std::string & path,
  std::string & error)
{
  try {
    const auto value = node.as<std::string>();
    if (value.size() == 1U) {
      return static_cast<char16_t>(static_cast<unsigned char>(value.front()));
    }
  } catch (const std::exception &) {
  }
  return checkedInteger<char16_t>(node, path, error);
}

} // namespace detail

} // namespace ros2_livekit_bridge::ros2_cli
