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

#include "ros2_livekit_bridge/utils/image_conversion.hpp"

#include <cstring>

namespace ros2_livekit_bridge::utils {

std::optional<livekit::VideoFrame> makeRgbaVideoFrame(int width, int height, const std::uint8_t* rgba,
                                                      std::size_t rgba_size) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const std::size_t expected_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  if (rgba_size != expected_size) {
    return std::nullopt;
  }
  if (rgba == nullptr) {
    return std::nullopt;
  }

  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::RGBA);
  std::memcpy(frame.data(), rgba, rgba_size);
  return frame;
}

bool convertToRgba(const sensor_msgs::msg::Image& image, std::vector<std::uint8_t>& out) {
  const std::size_t num_pixels = static_cast<std::size_t>(image.width) * image.height;
  out.resize(num_pixels * 4);

  const auto& encoding = image.encoding;

  if (encoding == "rgba8") {
    if (image.step == image.width * 4) {
      std::memcpy(out.data(), image.data.data(), num_pixels * 4);
    } else {
      for (std::uint32_t y = 0; y < image.height; ++y) {
        std::memcpy(out.data() + y * image.width * 4, image.data.data() + y * image.step, image.width * 4);
      }
    }
    return true;
  }

  if (encoding == "rgb8") {
    for (std::uint32_t y = 0; y < image.height; ++y) {
      const auto* row = image.data.data() + y * image.step;
      for (std::uint32_t x = 0; x < image.width; ++x) {
        const auto* px = row + x * 3;
        auto* dst = out.data() + (y * image.width + x) * 4;
        dst[0] = px[0];
        dst[1] = px[1];
        dst[2] = px[2];
        dst[3] = 255;
      }
    }
    return true;
  }

  if (encoding == "bgr8") {
    for (std::uint32_t y = 0; y < image.height; ++y) {
      const auto* row = image.data.data() + y * image.step;
      for (std::uint32_t x = 0; x < image.width; ++x) {
        const auto* px = row + x * 3;
        auto* dst = out.data() + (y * image.width + x) * 4;
        dst[0] = px[2];
        dst[1] = px[1];
        dst[2] = px[0];
        dst[3] = 255;
      }
    }
    return true;
  }

  if (encoding == "bgra8") {
    for (std::uint32_t y = 0; y < image.height; ++y) {
      const auto* row = image.data.data() + y * image.step;
      for (std::uint32_t x = 0; x < image.width; ++x) {
        const auto* px = row + x * 4;
        auto* dst = out.data() + (y * image.width + x) * 4;
        dst[0] = px[2];
        dst[1] = px[1];
        dst[2] = px[0];
        dst[3] = px[3];
      }
    }
    return true;
  }

  if (encoding == "mono8") {
    for (std::uint32_t y = 0; y < image.height; ++y) {
      const auto* row = image.data.data() + y * image.step;
      for (std::uint32_t x = 0; x < image.width; ++x) {
        auto* dst = out.data() + (y * image.width + x) * 4;
        dst[0] = row[x];
        dst[1] = row[x];
        dst[2] = row[x];
        dst[3] = 255;
      }
    }
    return true;
  }

  return false;
}

} // namespace ros2_livekit_bridge::utils
