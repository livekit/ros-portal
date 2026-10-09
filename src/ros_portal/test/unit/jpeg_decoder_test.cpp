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

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ros_portal/utils/jpeg_encoder.hpp"

namespace ros_portal::utils {
namespace {

std::vector<std::uint8_t> encodeI420(const int width, const int height, const std::uint8_t y, const std::uint8_t u,
                                     const std::uint8_t v) {
  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::I420);
  const auto luma_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto chroma_size = static_cast<std::size_t>((width + 1) / 2) * static_cast<std::size_t>((height + 1) / 2);
  std::memset(frame.data(), y, luma_size);
  std::memset(frame.data() + luma_size, u, chroma_size);
  std::memset(frame.data() + luma_size + chroma_size, v, chroma_size);
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;
  EXPECT_TRUE(encoder.encodeI420(frame, jpeg));
  return jpeg;
}

/// Encode a solid RGB image with the given chroma subsampling.
std::vector<std::uint8_t> encodeRgb(const int width, const int height, const int subsampling, const std::uint8_t r,
                                    const std::uint8_t g, const std::uint8_t b) {
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  for (std::size_t i = 0; i < rgb.size(); i += 3U) {
    rgb[i] = r;
    rgb[i + 1U] = g;
    rgb[i + 2U] = b;
  }
  tjhandle compressor = tjInitCompress();
  unsigned char* jpeg = nullptr;
  unsigned long jpeg_size = 0;
  EXPECT_EQ(tjCompress2(compressor, rgb.data(), width, 0, height, TJPF_RGB, &jpeg, &jpeg_size, subsampling, 95, 0), 0);
  std::vector<std::uint8_t> out(jpeg, jpeg + jpeg_size);
  tjFree(jpeg);
  tjDestroy(compressor);
  return out;
}

} // namespace

TEST(JpegDecoderTest, RecognizesJpegFormats) {
  EXPECT_TRUE(isJpegFormat("jpeg"));
  EXPECT_TRUE(isJpegFormat("JPG"));
  EXPECT_TRUE(isJpegFormat("bgr8; jpeg compressed bgr8"));
  EXPECT_FALSE(isJpegFormat("png"));
  EXPECT_FALSE(isJpegFormat("bgr8; png compressed bgr8"));
  EXPECT_FALSE(isJpegFormat(""));
}

TEST(JpegDecoderTest, Decodes420JpegToI420WithSourceSize) {
  const auto jpeg = encodeI420(33, 17, 100, 128, 128);
  JpegDecoder decoder;

  const auto* frame = decoder.decode(jpeg.data(), jpeg.size());

  ASSERT_NE(frame, nullptr) << decoder.lastError();
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::I420);
  EXPECT_EQ(frame->width(), 33);
  EXPECT_EQ(frame->height(), 17);
  EXPECT_NEAR(frame->data()[0], 100, 3);
}

TEST(JpegDecoderTest, Decodes444JpegToRgb24WithColorKept) {
  const auto jpeg = encodeRgb(16, 16, TJSAMP_444, 255, 0, 0);
  JpegDecoder decoder;

  const auto* frame = decoder.decode(jpeg.data(), jpeg.size());

  ASSERT_NE(frame, nullptr) << decoder.lastError();
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::RGB24);
  const std::size_t center = (8U * 16U + 8U) * 3U;
  EXPECT_GT(frame->data()[center], 240);
  EXPECT_LT(frame->data()[center + 1U], 15);
  EXPECT_LT(frame->data()[center + 2U], 15);
}

TEST(JpegDecoderTest, DecodesGrayscaleJpegToI420WithNeutralChroma) {
  std::vector<std::uint8_t> gray(16U * 16U, 90U);
  tjhandle compressor = tjInitCompress();
  unsigned char* jpeg = nullptr;
  unsigned long jpeg_size = 0;
  ASSERT_EQ(tjCompress2(compressor, gray.data(), 16, 0, 16, TJPF_GRAY, &jpeg, &jpeg_size, TJSAMP_GRAY, 95, 0), 0);
  const std::vector<std::uint8_t> data(jpeg, jpeg + jpeg_size);
  tjFree(jpeg);
  tjDestroy(compressor);
  JpegDecoder decoder;

  const auto* frame = decoder.decode(data.data(), data.size());

  ASSERT_NE(frame, nullptr) << decoder.lastError();
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::I420);
  EXPECT_NEAR(frame->data()[0], 90, 3);
  EXPECT_EQ(frame->data()[256], 128);
  EXPECT_EQ(frame->data()[frame->dataSize() - 1U], 128);
}

TEST(JpegDecoderTest, ReusesOutputFrameForSameSize) {
  const auto jpeg = encodeI420(32, 32, 100, 128, 128);
  JpegDecoder decoder;

  const auto* first = decoder.decode(jpeg.data(), jpeg.size());
  ASSERT_NE(first, nullptr);
  const auto* first_pixels = first->data();
  const auto* second = decoder.decode(jpeg.data(), jpeg.size());

  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->data(), first_pixels);
}

TEST(JpegDecoderTest, RejectsInvalidAndTruncatedData) {
  JpegDecoder decoder;
  const std::vector<std::uint8_t> garbage{1, 2, 3, 4, 5};
  EXPECT_EQ(decoder.decode(garbage.data(), garbage.size()), nullptr);
  EXPECT_FALSE(decoder.lastError().empty());
  EXPECT_EQ(decoder.decode(nullptr, 0), nullptr);

  // A truncated JPEG decodes only with a warning, so it must be rejected.
  auto jpeg = encodeRgb(64, 64, TJSAMP_420, 10, 200, 30);
  jpeg.resize(jpeg.size() / 2U);
  EXPECT_EQ(decoder.decode(jpeg.data(), jpeg.size()), nullptr);
}

} // namespace ros_portal::utils
