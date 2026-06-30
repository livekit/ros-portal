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

#include <cstddef>

namespace ros2_livekit_bridge::utils {

namespace {

constexpr char kEncodeTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Map one base64 character to its 6-bit value, or -1 when invalid.
int decodeChar(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return ch - 'A';
  }
  if (ch >= 'a' && ch <= 'z') {
    return ch - 'a' + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return ch - '0' + 52;
  }
  if (ch == '+') {
    return 62;
  }
  if (ch == '/') {
    return 63;
  }
  return -1;
}

} // namespace

std::string base64Encode(const std::vector<std::uint8_t> &bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2U) / 3U) * 4U);

  const std::size_t size = bytes.size();
  std::size_t i = 0;
  for (; i + 3U <= size; i += 3U) {
    const std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                 (static_cast<std::uint32_t>(bytes[i + 1U]) << 8) |
                                 static_cast<std::uint32_t>(bytes[i + 2U]);
    out.push_back(kEncodeTable[(triple >> 18) & 0x3FU]);
    out.push_back(kEncodeTable[(triple >> 12) & 0x3FU]);
    out.push_back(kEncodeTable[(triple >> 6) & 0x3FU]);
    out.push_back(kEncodeTable[triple & 0x3FU]);
  }

  const std::size_t remaining = size - i;
  if (remaining == 1U) {
    const std::uint32_t triple = static_cast<std::uint32_t>(bytes[i]) << 16;
    out.push_back(kEncodeTable[(triple >> 18) & 0x3FU]);
    out.push_back(kEncodeTable[(triple >> 12) & 0x3FU]);
    out.push_back('=');
    out.push_back('=');
  } else if (remaining == 2U) {
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(bytes[i]) << 16) | (static_cast<std::uint32_t>(bytes[i + 1U]) << 8);
    out.push_back(kEncodeTable[(triple >> 18) & 0x3FU]);
    out.push_back(kEncodeTable[(triple >> 12) & 0x3FU]);
    out.push_back(kEncodeTable[(triple >> 6) & 0x3FU]);
    out.push_back('=');
  }

  return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string &text) {
  if (text.size() % 4U != 0U) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> out;
  out.reserve((text.size() / 4U) * 3U);

  for (std::size_t i = 0; i < text.size(); i += 4U) {
    const char c0 = text[i];
    const char c1 = text[i + 1U];
    const char c2 = text[i + 2U];
    const char c3 = text[i + 3U];
    const bool last_group = (i + 4U == text.size());

    const int v0 = decodeChar(c0);
    const int v1 = decodeChar(c1);
    if (v0 < 0 || v1 < 0) {
      return std::nullopt;
    }

    if (c2 == '=') {
      // One byte of data: '=' may only appear as the last two characters.
      if (!last_group || c3 != '=') {
        return std::nullopt;
      }
      out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
      continue;
    }

    const int v2 = decodeChar(c2);
    if (v2 < 0) {
      return std::nullopt;
    }

    if (c3 == '=') {
      // Two bytes of data.
      if (!last_group) {
        return std::nullopt;
      }
      out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
      out.push_back(static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)));
      continue;
    }

    const int v3 = decodeChar(c3);
    if (v3 < 0) {
      return std::nullopt;
    }
    out.push_back(static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4)));
    out.push_back(static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2)));
    out.push_back(static_cast<std::uint8_t>(((v2 & 0x03) << 6) | v3));
  }

  return out;
}

} // namespace ros2_livekit_bridge::utils
