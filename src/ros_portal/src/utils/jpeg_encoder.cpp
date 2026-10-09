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

#include "ros_portal/utils/jpeg_encoder.hpp"

#include <turbojpeg.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

namespace ros_portal::utils {

JpegEncoder::JpegEncoder(const int quality) : handle_(tjInitCompress()), quality_(std::clamp(quality, 1, 100)) {}

JpegEncoder::~JpegEncoder() {
  if (buffer_ != nullptr) {
    tjFree(buffer_);
  }
  if (handle_ != nullptr) {
    tjDestroy(handle_);
  }
}

bool JpegEncoder::encodeI420(const livekit::VideoFrame& frame, std::vector<std::uint8_t>& out) {
  if (handle_ == nullptr) {
    last_error_ = "TurboJPEG compressor is unavailable";
    return false;
  }
  if (frame.type() != livekit::VideoBufferType::I420) {
    last_error_ = "frame is not I420";
    return false;
  }

  const int width = frame.width();
  const int height = frame.height();
  if (width <= 0 || height <= 0) {
    last_error_ = "frame has no pixels";
    return false;
  }

  const auto luma_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const int chroma_width = (width + 1) / 2;
  const auto chroma_size = static_cast<std::size_t>(chroma_width) * static_cast<std::size_t>((height + 1) / 2);
  if (frame.dataSize() != luma_size + 2U * chroma_size) {
    last_error_ = "frame buffer size " + std::to_string(frame.dataSize()) + " does not match packed I420 " +
                  std::to_string(width) + "x" + std::to_string(height);
    return false;
  }

  const unsigned long required_capacity = tjBufSize(width, height, TJSAMP_420);
  // tjBufSize() returns (unsigned long)-1 on error, and tjAlloc() takes an int.
  if (required_capacity > static_cast<unsigned long>(std::numeric_limits<int>::max())) {
    last_error_ = "frame is too large to encode";
    return false;
  }
  if (buffer_capacity_ < required_capacity) {
    if (buffer_ != nullptr) {
      tjFree(buffer_);
    }
    buffer_ = tjAlloc(static_cast<int>(required_capacity));
    buffer_capacity_ = buffer_ != nullptr ? required_capacity : 0U;
    if (buffer_ == nullptr) {
      last_error_ = "failed to allocate the JPEG output buffer";
      return false;
    }
  }

  const std::uint8_t* luma = frame.data();
  const unsigned char* planes[3] = {luma, luma + luma_size, luma + luma_size + chroma_size};
  const int strides[3] = {width, chroma_width, chroma_width};
  unsigned char* jpeg = buffer_;
  unsigned long jpeg_size = buffer_capacity_;
  // NOREALLOC: the buffer already holds tjBufSize() bytes, the worst case for this frame size.
  if (tjCompressFromYUVPlanes(handle_, planes, width, strides, height, TJSAMP_420, &jpeg, &jpeg_size, quality_,
                              TJFLAG_NOREALLOC) != 0) {
    last_error_ = tjGetErrorStr2(handle_);
    return false;
  }

  out.assign(jpeg, jpeg + jpeg_size);
  return true;
}

} // namespace ros_portal::utils
