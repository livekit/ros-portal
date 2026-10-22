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

#include "ros_portal/utils/jpeg_decoder.hpp"

#include <turbojpeg.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

namespace ros_portal::utils {

namespace {

/// @brief Neutral chroma value for grayscale I420 frames.
constexpr std::uint8_t kNeutralChroma = 128U;

bool containsIgnoreCase(std::string_view text, std::string_view needle) {
  return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](const char lhs, const char rhs) {
           return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
         }) != text.end();
}

} // namespace

bool isJpegFormat(std::string_view format) {
  return containsIgnoreCase(format, "jpeg") || containsIgnoreCase(format, "jpg");
}

JpegDecoder::JpegDecoder() : handle_(tjInitDecompress()) {}

JpegDecoder::~JpegDecoder() {
  if (handle_ != nullptr) {
    tjDestroy(handle_);
  }
}

const livekit::VideoFrame* JpegDecoder::decode(const std::uint8_t* jpeg, const std::size_t size) {
  if (handle_ == nullptr) {
    last_error_ = "TurboJPEG decompressor is unavailable";
    return nullptr;
  }
  if (jpeg == nullptr || size == 0U || size > std::numeric_limits<unsigned long>::max()) {
    last_error_ = "JPEG data is empty";
    return nullptr;
  }

  const auto jpeg_size = static_cast<unsigned long>(size);
  int width = 0;
  int height = 0;
  int subsampling = 0;
  int colorspace = 0;
  if (tjDecompressHeader3(handle_, jpeg, jpeg_size, &width, &height, &subsampling, &colorspace) != 0) {
    last_error_ = tjGetErrorStr2(handle_);
    return nullptr;
  }
  if (width <= 0 || height <= 0) {
    last_error_ = "JPEG has no pixels";
    return nullptr;
  }

  bool fresh = false;
  int result = 0;
  if (subsampling == TJSAMP_420 || subsampling == TJSAMP_GRAY) {
    auto& frame = reuse(width, height, livekit::VideoBufferType::I420, fresh);
    const int chroma_width = (width + 1) / 2;
    const auto luma_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto chroma_size = static_cast<std::size_t>(chroma_width) * static_cast<std::size_t>((height + 1) / 2);
    if (fresh && subsampling == TJSAMP_GRAY) {
      // Grayscale JPEGs only write the Y plane.
      std::memset(frame.data() + luma_size, kNeutralChroma, 2U * chroma_size);
    }
    unsigned char* planes[3] = {frame.data(), frame.data() + luma_size, frame.data() + luma_size + chroma_size};
    int strides[3] = {width, chroma_width, chroma_width};
    result = tjDecompressToYUVPlanes(handle_, jpeg, jpeg_size, planes, width, strides, height, 0);
  } else {
    auto& frame = reuse(width, height, livekit::VideoBufferType::RGB24, fresh);
    result = tjDecompress2(handle_, jpeg, jpeg_size, frame.data(), width, width * 3, height, TJPF_RGB, 0);
  }

  // A warning means the image decoded only partly, for example from a
  // truncated JPEG. Drop it rather than send gray or garbled rows.
  if (result != 0) {
    last_error_ = tjGetErrorStr2(handle_);
    return nullptr;
  }
  return &frame_;
}

livekit::VideoFrame& JpegDecoder::reuse(const int width, const int height, const livekit::VideoBufferType type,
                                        bool& fresh) {
  fresh = frame_.width() != width || frame_.height() != height || frame_.type() != type || frame_.dataSize() == 0U;
  if (fresh) {
    frame_ = livekit::VideoFrame::create(width, height, type);
  }
  return frame_;
}

} // namespace ros_portal::utils
