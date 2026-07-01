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

/// @brief These helpers form the bridge's ROS image-to-video pipeline: pixel data is
/// normalized to tightly packed RGBA8, then wrapped in a `livekit::VideoFrame` for capture by a video source.

#ifndef ROS2_LIVEKIT_BRIDGE__UTILS__IMAGE_CONVERSION_HPP_
#define ROS2_LIVEKIT_BRIDGE__UTILS__IMAGE_CONVERSION_HPP_

#include <livekit/video_frame.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sensor_msgs/msg/image.hpp>
#include <vector>

namespace ros2_livekit_bridge::utils {

/// @brief Convert a ROS image message to tightly packed RGBA8 pixels.
///
/// Resizes @p out to `width * height * 4` bytes and writes row-major RGBA8
/// data with no row padding. Supports `rgba8`, `rgb8`, `bgr8`, `bgra8`, and
/// `mono8` encodings. Padded source rows (`step` larger than the packed row
/// size) are handled for all supported encodings.
///
/// @param image Source ROS image message.
/// @param out Destination buffer resized on success.
/// @return `true` when @p image uses a supported encoding, otherwise `false`.
bool convertToRgba(const sensor_msgs::msg::Image& image, std::vector<std::uint8_t>& out);

/// @brief Wrap an RGBA8 pixel buffer in a LiveKit video frame.
///
/// Validates that @p width and @p height are positive and that @p rgba_size
/// equals `width * height * 4`. On success, copies @p rgba into a newly
/// created `livekit::VideoFrame` with buffer type RGBA.
///
/// @param width Frame width in pixels.
/// @param height Frame height in pixels.
/// @param rgba Tightly packed RGBA8 pixel data.
/// @param rgba_size Size of @p rgba in bytes.
/// @return A populated video frame, or `std::nullopt` when validation fails.
std::optional<livekit::VideoFrame> makeRgbaVideoFrame(int width, int height, const std::uint8_t* rgba,
                                                      std::size_t rgba_size);

} // namespace ros2_livekit_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__IMAGE_CONVERSION_HPP_
