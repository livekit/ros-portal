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

#include "ros2_livekit_bridge/utils/base64.hpp"

#include <array>

namespace ros2_livekit_bridge::utils {

namespace {

constexpr char kEncodeTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Reverse lookup table: base64 char -> 6-bit value, or -1 when invalid.
std::array<std::int8_t, 256> makeDecodeTable() {
  std::array<std::int8_t, 256> table{};
  table.fill(-1);
  for (std::int8_t i = 0; i < 64; ++i) {
    table[static_cast<unsigned char>(kEncodeTable[i])] = i;
  }
  return table;
}

} // namespace

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
  std::string encoded;
  if (size == 0 || data == nullptr) {
    return encoded;
  }
  encoded.reserve(((size + 2) / 3) * 4);

  std::size_t i = 0;
  for (; i + 3 <= size; i += 3) {
    const std::uint32_t triple = (static_cast<std::uint32_t>(data[i]) << 16) |
                                 (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                 static_cast<std::uint32_t>(data[i + 2]);
    encoded.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
    encoded.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
    encoded.push_back(kEncodeTable[(triple >> 6) & 0x3F]);
    encoded.push_back(kEncodeTable[triple & 0x3F]);
  }

  const std::size_t remaining = size - i;
  if (remaining == 1) {
    const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
    encoded.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
    encoded.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
    encoded.push_back('=');
    encoded.push_back('=');
  } else if (remaining == 2) {
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
    encoded.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
    encoded.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
    encoded.push_back(kEncodeTable[(triple >> 6) & 0x3F]);
    encoded.push_back('=');
  }

  return encoded;
}

std::string base64Encode(const std::vector<std::uint8_t>& data) { return base64Encode(data.data(), data.size()); }

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& encoded) {
  static const std::array<std::int8_t, 256> decode_table = makeDecodeTable();

  if (encoded.size() % 4 != 0) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> decoded;
  decoded.reserve((encoded.size() / 4) * 3);

  for (std::size_t i = 0; i < encoded.size(); i += 4) {
    const char c0 = encoded[i];
    const char c1 = encoded[i + 1];
    const char c2 = encoded[i + 2];
    const char c3 = encoded[i + 3];

    // Padding may only appear in the final quartet, and '=' in position 2
    // implies '=' in position 3.
    const bool is_last = (i + 4 == encoded.size());
    const bool pad2 = c2 == '=';
    const bool pad3 = c3 == '=';
    if ((pad2 || pad3) && !is_last) {
      return std::nullopt;
    }
    if (pad2 && !pad3) {
      return std::nullopt;
    }

    const std::int8_t v0 = decode_table[static_cast<unsigned char>(c0)];
    const std::int8_t v1 = decode_table[static_cast<unsigned char>(c1)];
    if (v0 < 0 || v1 < 0) {
      return std::nullopt;
    }

    decoded.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));

    if (!pad2) {
      const std::int8_t v2 = decode_table[static_cast<unsigned char>(c2)];
      if (v2 < 0) {
        return std::nullopt;
      }
      decoded.push_back(static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)));

      if (!pad3) {
        const std::int8_t v3 = decode_table[static_cast<unsigned char>(c3)];
        if (v3 < 0) {
          return std::nullopt;
        }
        decoded.push_back(static_cast<std::uint8_t>(((v2 & 0x03) << 6) | v3));
      }
    }
  }

  return decoded;
}

} // namespace ros2_livekit_bridge::utils
