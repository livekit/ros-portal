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

#include <livekit/video_frame.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ros_portal::utils {

/// @brief Return whether a `sensor_msgs/CompressedImage.format` names JPEG data.
///
/// Accepts plain values such as `jpeg` and image_transport values such as
/// `bgr8; jpeg compressed bgr8`, case-insensitively.
bool isJpegFormat(std::string_view format);

/// @brief Reusable TurboJPEG decompressor that produces LiveKit video frames.
///
/// A 4:2:0 JPEG decodes straight into an I420 frame with no color conversion,
/// and a grayscale JPEG into the Y plane of an I420 frame with neutral chroma.
/// Other subsampling decodes to RGB24. The output frame is reused while the
/// size and layout stay the same. An instance is not thread-safe; use one per
/// topic.
class JpegDecoder {
public:
  /// @brief Create a decompressor.
  JpegDecoder();

  /// @brief Release the TurboJPEG handle.
  ~JpegDecoder();

  JpegDecoder(const JpegDecoder&) = delete;
  JpegDecoder& operator=(const JpegDecoder&) = delete;
  JpegDecoder(JpegDecoder&&) = delete;
  JpegDecoder& operator=(JpegDecoder&&) = delete;

  /// @brief Decode one JPEG image.
  /// @param jpeg JPEG bytes.
  /// @param size Number of bytes at @p jpeg.
  /// @return The decoded frame, valid until the next call, or nullptr when the
  /// data is not a complete, valid JPEG. A JPEG that decodes only with a
  /// warning, such as a truncated one, is also rejected. On failure,
  /// @ref lastError describes the cause.
  [[nodiscard]] const livekit::VideoFrame* decode(const std::uint8_t* jpeg, std::size_t size);

  /// @brief Return the cause of the most recent decode failure.
  const std::string& lastError() const noexcept { return last_error_; }

private:
  /// @brief Return the reused output frame, reallocating it only when the size
  /// or layout changes.
  /// @param fresh Set to true when the frame was reallocated.
  livekit::VideoFrame& reuse(int width, int height, livekit::VideoBufferType type, bool& fresh);

  /// @brief Opaque TurboJPEG decompressor handle (`tjhandle`).
  void* handle_{nullptr};
  /// @brief Reused output frame.
  livekit::VideoFrame frame_;
  /// @brief Cause of the most recent decode failure.
  std::string last_error_;
};

} // namespace ros_portal::utils
