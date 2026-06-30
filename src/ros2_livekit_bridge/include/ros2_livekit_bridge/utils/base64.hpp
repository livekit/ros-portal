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

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils {

/// @brief Encode raw bytes as standard ('+'/'/' alphabet, '=' padded) base64.
///
/// Used to carry binary CDR service payloads inside the UTF-8 string payload of
/// a LiveKit RPC. An empty input produces an empty string.
///
/// @param bytes Raw bytes to encode.
/// @return Base64 text.
std::string base64Encode(const std::vector<std::uint8_t> &bytes);

/// @brief Decode standard base64 text back into raw bytes.
///
/// Rejects input whose length is not a multiple of four, that contains
/// characters outside the standard alphabet, or whose '=' padding is misplaced.
///
/// @param text Base64 text to decode.
/// @return Decoded bytes, or std::nullopt when @p text is not valid base64.
std::optional<std::vector<std::uint8_t>> base64Decode(const std::string &text);

} // namespace ros2_livekit_bridge::utils
