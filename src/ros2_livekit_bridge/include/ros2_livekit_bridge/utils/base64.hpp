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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils {

/// @brief Base64-encode a byte buffer (standard alphabet, '=' padding).
///
/// LiveKit RPC payloads are UTF-8 strings, so binary CDR must be encoded before
/// it can be carried in an RPC request.
/// @param data Pointer to the bytes to encode; may be null only when @p size is 0.
/// @param size Number of bytes to encode.
/// @return The base64 text.
std::string base64Encode(const std::uint8_t* data, std::size_t size);

/// @brief Base64-encode a byte vector. @see base64Encode(const std::uint8_t*, std::size_t).
std::string base64Encode(const std::vector<std::uint8_t>& data);

/// @brief Decode standard base64 text back into bytes.
///
/// Rejects input whose length is not a multiple of four or that contains
/// characters outside the standard alphabet / padding.
/// @param encoded Base64 text.
/// @return The decoded bytes, or std::nullopt when @p encoded is malformed.
std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& encoded);

} // namespace ros2_livekit_bridge::utils
