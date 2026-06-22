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

#include "ros2_livekit_bridge/ros2_cli/utils.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <nlohmann/json.hpp>

namespace ros2_livekit_bridge::ros2_cli
{

std::string requiredStringField(
  const nlohmann::json & body,
  const char *field_name,
  const char *missing_message,
  const char *empty_message)
{
  const auto field = body.find(field_name);
  if (field == body.end() || !field->is_string()) {
    throw std::invalid_argument(missing_message);
  }

  auto value = rightTrim(leftTrim(field->get<std::string>()));
  if (value.empty()) {
    throw std::invalid_argument(empty_message);
  }
  return value;
}

bool topicTypeMatches(
  const std::vector<std::string> & graph_types,
  const std::string & msg_type)
{
  return std::find(graph_types.begin(), graph_types.end(), msg_type) !=
         graph_types.end();
}

bool hasHiddenNameToken(const std::string & name)
{
  size_t token_start = 0;
  while (token_start < name.size()) {
    while (token_start < name.size() && name[token_start] == '/') {
      ++token_start;
    }
    if (token_start >= name.size()) {
      break;
    }

    const auto token_end = name.find('/', token_start);
    if (name[token_start] == '_') {
      return true;
    }
    if (token_end == std::string::npos) {
      break;
    }
    token_start = token_end + 1;
  }
  return false;
}

std::string joinTypes(const std::vector<std::string> & types)
{
  std::ostringstream stream;
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << types[i];
  }
  return stream.str();
}

std::string base64Encode(const std::vector<std::uint8_t> & bytes)
{
  static constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string encoded;
  encoded.reserve(((bytes.size() + 2U) / 3U) * 4U);

  for (std::size_t i = 0; i < bytes.size(); i += 3U) {
    const std::uint32_t octet_a = bytes[i];
    const std::uint32_t octet_b = (i + 1U) < bytes.size() ? bytes[i + 1U] : 0U;
    const std::uint32_t octet_c = (i + 2U) < bytes.size() ? bytes[i + 2U] : 0U;
    const std::uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

    encoded.push_back(kAlphabet[(triple >> 18U) & 0x3FU]);
    encoded.push_back(kAlphabet[(triple >> 12U) & 0x3FU]);
    encoded.push_back((i + 1U) < bytes.size() ?
      kAlphabet[(triple >> 6U) & 0x3FU] : '=');
    encoded.push_back((i + 2U) < bytes.size() ?
      kAlphabet[triple & 0x3FU] : '=');
  }

  return encoded;
}

std::optional<std::vector<std::uint8_t>> base64Decode(
  const std::string & encoded,
  std::string & error)
{
  static const std::array<int, 256> kDecodeTable = []() {
      std::array<int, 256> table{};
      table.fill(-1);
      for (int value = 0; value < 26; ++value) {
        table[static_cast<std::size_t>('A' + value)] = value;
        table[static_cast<std::size_t>('a' + value)] = value + 26;
      }
      for (int value = 0; value < 10; ++value) {
        table[static_cast<std::size_t>('0' + value)] = value + 52;
      }
      table[static_cast<std::size_t>('+')] = 62;
      table[static_cast<std::size_t>('/')] = 63;
      return table;
    }();

  if (encoded.size() % 4U != 0U) {
    error = "payload_base64 must be padded standard base64";
    return std::nullopt;
  }

  std::vector<std::uint8_t> decoded;
  decoded.reserve((encoded.size() / 4U) * 3U);

  for (std::size_t i = 0; i < encoded.size(); i += 4U) {
    const bool pad_two = encoded[i + 2U] == '=';
    const bool pad_three = encoded[i + 3U] == '=';
    if (encoded[i] == '=' || encoded[i + 1U] == '=' ||
      (pad_two && !pad_three))
    {
      error = "payload_base64 is not valid base64";
      return std::nullopt;
    }

    const auto value = [&](std::size_t index) -> std::optional<int> {
        const auto ch = static_cast<unsigned char>(encoded[index]);
        if (encoded[index] == '=') {
          return 0;
        }
        const int decoded_value = kDecodeTable[ch];
        if (decoded_value < 0) {
          return std::nullopt;
        }
        return decoded_value;
      };

    const auto sextet_a = value(i);
    const auto sextet_b = value(i + 1U);
    const auto sextet_c = value(i + 2U);
    const auto sextet_d = value(i + 3U);
    if (!sextet_a || !sextet_b || !sextet_c || !sextet_d) {
      error = "payload_base64 is not valid base64";
      return std::nullopt;
    }

    const std::uint32_t triple =
      (static_cast<std::uint32_t>(*sextet_a) << 18U) |
      (static_cast<std::uint32_t>(*sextet_b) << 12U) |
      (static_cast<std::uint32_t>(*sextet_c) << 6U) |
      static_cast<std::uint32_t>(*sextet_d);

    decoded.push_back(static_cast<std::uint8_t>((triple >> 16U) & 0xFFU));
    if (!pad_two) {
      decoded.push_back(static_cast<std::uint8_t>((triple >> 8U) & 0xFFU));
    }
    if (!pad_three) {
      decoded.push_back(static_cast<std::uint8_t>(triple & 0xFFU));
    }

    if ((pad_two || pad_three) && i + 4U != encoded.size()) {
      error = "payload_base64 padding may only appear at the end";
      return std::nullopt;
    }
  }

  return decoded;
}

} // namespace ros2_livekit_bridge::ros2_cli
