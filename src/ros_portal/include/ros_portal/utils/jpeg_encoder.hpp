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

#include <cstdint>
#include <string>
#include <vector>

namespace ros_portal::utils {

/// @brief JPEG quality (1-100) used for inbound video frames.
inline constexpr int kDefaultJpegQuality = 90;

/// @brief Reusable TurboJPEG compressor for decoded LiveKit video frames.
///
/// Each instance owns one TurboJPEG handle and one output buffer. Both are
/// reused across frames, so steady-state encoding does not allocate unless the
/// frame size grows. An instance is not thread-safe; use one per reader thread.
class JpegEncoder {
public:
  /// @brief Create a compressor.
  /// @param quality JPEG quality from 1 to 100. Values outside the range are
  /// clamped.
  explicit JpegEncoder(int quality = kDefaultJpegQuality);

  /// @brief Release the TurboJPEG handle and output buffer.
  ~JpegEncoder();

  JpegEncoder(const JpegEncoder&) = delete;
  JpegEncoder& operator=(const JpegEncoder&) = delete;
  JpegEncoder(JpegEncoder&&) = delete;
  JpegEncoder& operator=(JpegEncoder&&) = delete;

  /// @brief Encode one tightly packed I420 frame as a 4:2:0 JPEG.
  ///
  /// The frame must use @ref livekit::VideoBufferType::I420 with the Y, U and V
  /// planes packed back to back and no row padding, which is the layout that
  /// @ref livekit::VideoStream delivers.
  /// @param frame Decoded I420 frame.
  /// @param out Receives the JPEG bytes. Its capacity is reused across calls.
  /// @return True on success. On failure, @p out is unchanged and
  /// @ref lastError describes the cause.
  [[nodiscard]] bool encodeI420(const livekit::VideoFrame& frame, std::vector<std::uint8_t>& out);

  /// @brief Return the cause of the most recent encode failure.
  const std::string& lastError() const noexcept { return last_error_; }

private:
  /// @brief Opaque TurboJPEG compressor handle (`tjhandle`).
  void* handle_{nullptr};
  /// @brief TurboJPEG-allocated output buffer reused across frames.
  unsigned char* buffer_{nullptr};
  /// @brief Capacity of @ref buffer_ in bytes.
  unsigned long buffer_capacity_{0U};
  /// @brief JPEG quality from 1 to 100.
  int quality_;
  /// @brief Cause of the most recent encode failure.
  std::string last_error_;
};

} // namespace ros_portal::utils
