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

#include "ros_portal/utils/image_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace ros_portal::utils {

namespace {

/// @brief Neutral chroma value for grayscale I420 frames.
constexpr std::uint8_t kNeutralChroma = 128U;

/// @brief Return bytes per pixel for a supported ROS encoding, or 0.
std::uint32_t bytesPerPixel(const std::string& encoding) {
  if (encoding == "rgba8" || encoding == "bgra8") {
    return 4U;
  }
  if (encoding == "rgb8" || encoding == "bgr8") {
    return 3U;
  }
  if (encoding == "mono8") {
    return 1U;
  }
  return 0U;
}

/// @brief Return the LiveKit layout that matches a ROS encoding byte for byte.
std::optional<livekit::VideoBufferType> nativeLayout(const std::string& encoding) {
  if (encoding == "rgba8") {
    return livekit::VideoBufferType::RGBA;
  }
  if (encoding == "bgra8") {
    return livekit::VideoBufferType::BGRA;
  }
  if (encoding == "rgb8") {
    // LiveKit RGB24 is R,G,B in memory (libyuv RAW), the same as ROS rgb8.
    return livekit::VideoBufferType::RGB24;
  }
  return std::nullopt;
}

/// @brief Copy @p row_bytes from each source row into a packed destination.
void copyRows(const sensor_msgs::msg::Image& image, const std::size_t row_bytes, std::uint8_t* dst) {
  for (std::uint32_t y = 0; y < image.height; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * row_bytes,
                image.data.data() + static_cast<std::size_t>(y) * image.step, row_bytes);
  }
}

/// @brief Swap B and R into a packed R,G,B destination.
void bgrToRgb(const sensor_msgs::msg::Image& image, std::uint8_t* dst) {
  const std::size_t row_bytes = static_cast<std::size_t>(image.width) * 3U;
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::uint8_t* src = image.data.data() + static_cast<std::size_t>(y) * image.step;
    std::uint8_t* out = dst + static_cast<std::size_t>(y) * row_bytes;
    for (std::size_t i = 0; i < row_bytes; i += 3U) {
      out[i] = src[i + 2U];
      out[i + 1U] = src[i + 1U];
      out[i + 2U] = src[i];
    }
  }
}

} // namespace

const livekit::VideoFrame* ImageFrameBuilder::build(sensor_msgs::msg::Image& image) {
  const std::uint32_t bytes_per_pixel = bytesPerPixel(image.encoding);
  constexpr auto kMaxDimension = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  if (bytes_per_pixel == 0U || image.width == 0U || image.height == 0U || image.width > kMaxDimension ||
      image.height > kMaxDimension) {
    return nullptr;
  }
  const std::size_t row_bytes = static_cast<std::size_t>(image.width) * bytes_per_pixel;
  if (image.step < row_bytes || image.data.size() < static_cast<std::size_t>(image.step) * image.height) {
    return nullptr;
  }

  const auto width = static_cast<int>(image.width);
  const auto height = static_cast<int>(image.height);
  bool fresh = false;

  if (const auto layout = nativeLayout(image.encoding)) {
    if (image.step == row_bytes) {
      // Shrinking never reallocates, so this keeps the zero-copy hand-off.
      image.data.resize(row_bytes * image.height);
      moved_ = livekit::VideoFrame(width, height, *layout, std::move(image.data));
      return &moved_;
    }
    auto& frame = reuse(width, height, *layout, fresh);
    copyRows(image, row_bytes, frame.data());
    return &frame;
  }

  if (image.encoding == "bgr8") {
    auto& frame = reuse(width, height, livekit::VideoBufferType::RGB24, fresh);
    bgrToRgb(image, frame.data());
    return &frame;
  }

  // mono8: the Y plane is the image, and constant chroma makes it gray.
  auto& frame = reuse(width, height, livekit::VideoBufferType::I420, fresh);
  const std::size_t luma_size = row_bytes * image.height;
  if (fresh) {
    std::memset(frame.data() + luma_size, kNeutralChroma, frame.dataSize() - luma_size);
  }
  copyRows(image, row_bytes, frame.data());
  return &frame;
}

livekit::VideoFrame& ImageFrameBuilder::reuse(const int width, const int height, const livekit::VideoBufferType type,
                                              bool& fresh) {
  fresh = converted_.width() != width || converted_.height() != height || converted_.type() != type ||
          converted_.dataSize() == 0U;
  if (fresh) {
    converted_ = livekit::VideoFrame::create(width, height, type);
  }
  return converted_;
}

} // namespace ros_portal::utils
