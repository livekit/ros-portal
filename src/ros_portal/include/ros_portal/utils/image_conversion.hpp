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

#include <sensor_msgs/msg/image.hpp>

namespace ros_portal::utils {

/// @brief Build LiveKit video frames from ROS images with the fewest pixel copies.
///
/// LiveKit converts RGBA, BGRA, RGB24 (R,G,B byte order) and I420 frames to
/// I420 itself in one fused pass, so the builder hands over the closest native
/// layout instead of converting to RGBA first:
/// - `rgba8`, `bgra8` and `rgb8` with packed rows move the pixel buffer into
///   the frame without a copy.
/// - `bgr8` swaps channels into a reused RGB24 frame.
/// - `mono8` copies rows into the Y plane of a reused I420 frame with neutral
///   chroma.
/// - Packed encodings with row padding copy rows into a reused frame.
///
/// Use one instance per image topic. An instance is not thread-safe.
class ImageFrameBuilder {
public:
  /// @brief Build a frame for @p image.
  /// @param image Source image. On the zero-copy path its `data` is moved out.
  /// @return The frame to capture, valid until the next call, or nullptr when
  /// the encoding is unsupported, the image is empty, or `data` is smaller than
  /// `step * height`.
  const livekit::VideoFrame* build(sensor_msgs::msg::Image& image);

private:
  /// @brief Return the reused conversion frame, reallocating it only when the
  /// size or type changes.
  /// @param fresh Set to true when the frame was reallocated.
  livekit::VideoFrame& reuse(int width, int height, livekit::VideoBufferType type, bool& fresh);

  /// @brief Frame that owns a moved ROS pixel buffer.
  livekit::VideoFrame moved_;
  /// @brief Reused target for encodings that need a copy or conversion.
  livekit::VideoFrame converted_;
};

} // namespace ros_portal::utils
