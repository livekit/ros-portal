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
#include <string_view>
#include <vector>

namespace ros_portal::utils {

/// @brief Version byte that prefixes the ROS frame_id in video frame user data.
inline constexpr std::uint8_t kFrameIdUserDataVersion = 0x01U;
/// @brief Maximum ROS frame_id length, in bytes, carried in video frame user data.
inline constexpr std::size_t kMaxFrameIdUserDataLength = 255U;

/// @brief Encode a ROS frame_id as LiveKit video frame user data.
///
/// The layout is one version byte (@ref kFrameIdUserDataVersion) followed by
/// the frame_id bytes. Receivers use it to restore `header.frame_id`.
/// @param frame_id ROS `header.frame_id` of the source image.
/// @return Encoded user data, or std::nullopt when @p frame_id is longer than
/// @ref kMaxFrameIdUserDataLength.
std::optional<std::vector<std::uint8_t>> encodeFrameIdUserData(std::string_view frame_id);

/// @brief Decode a ROS frame_id from LiveKit video frame user data.
/// @param user_data User data received with a video frame.
/// @return A view of the frame_id inside @p user_data, valid while @p user_data
/// is unchanged, or std::nullopt when @p user_data is empty, has an unknown
/// version byte, or is longer than the encoded maximum.
std::optional<std::string_view> decodeFrameIdUserData(const std::vector<std::uint8_t>& user_data);

} // namespace ros_portal::utils
