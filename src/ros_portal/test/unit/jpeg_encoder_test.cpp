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

#include <gtest/gtest.h>
#include <turbojpeg.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ros_portal::utils {
namespace {

/// Build a packed I420 frame filled with one YUV color.
livekit::VideoFrame makeI420Frame(const int width, const int height, const std::uint8_t y, const std::uint8_t u,
                                  const std::uint8_t v) {
  auto frame = livekit::VideoFrame::create(width, height, livekit::VideoBufferType::I420);
  const auto luma_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto chroma_size = static_cast<std::size_t>((width + 1) / 2) * static_cast<std::size_t>((height + 1) / 2);
  std::memset(frame.data(), y, luma_size);
  std::memset(frame.data() + luma_size, u, chroma_size);
  std::memset(frame.data() + luma_size + chroma_size, v, chroma_size);
  return frame;
}

struct JpegHeader {
  int width{0};
  int height{0};
  int subsampling{-1};
};

JpegHeader readHeader(const std::vector<std::uint8_t>& jpeg) {
  JpegHeader header;
  int colorspace = 0;
  tjhandle decompressor = tjInitDecompress();
  EXPECT_EQ(tjDecompressHeader3(decompressor, jpeg.data(), static_cast<unsigned long>(jpeg.size()), &header.width,
                                &header.height, &header.subsampling, &colorspace),
            0);
  tjDestroy(decompressor);
  return header;
}

std::vector<std::uint8_t> decodeRgb(const std::vector<std::uint8_t>& jpeg, const int width, const int height) {
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  tjhandle decompressor = tjInitDecompress();
  EXPECT_EQ(tjDecompress2(decompressor, jpeg.data(), static_cast<unsigned long>(jpeg.size()), rgb.data(), width, 0,
                          height, TJPF_RGB, 0),
            0);
  tjDestroy(decompressor);
  return rgb;
}

} // namespace

TEST(JpegEncoderTest, EncodesI420FrameAsJpegWithSourceGeometry) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;

  ASSERT_TRUE(encoder.encodeI420(makeI420Frame(64, 48, 128, 128, 128), jpeg)) << encoder.lastError();

  ASSERT_GE(jpeg.size(), 4U);
  EXPECT_EQ(jpeg[0], 0xFF);
  EXPECT_EQ(jpeg[1], 0xD8);
  EXPECT_EQ(jpeg[jpeg.size() - 2U], 0xFF);
  EXPECT_EQ(jpeg[jpeg.size() - 1U], 0xD9);
  const auto header = readHeader(jpeg);
  EXPECT_EQ(header.width, 64);
  EXPECT_EQ(header.height, 48);
  EXPECT_EQ(header.subsampling, TJSAMP_420);
}

// BT.601 YUV (81, 90, 240) is red. Swapped U and V planes would decode as blue.
TEST(JpegEncoderTest, KeepsChromaPlanesInOrder) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;

  ASSERT_TRUE(encoder.encodeI420(makeI420Frame(32, 32, 81, 90, 240), jpeg)) << encoder.lastError();

  const auto rgb = decodeRgb(jpeg, 32, 32);
  const std::size_t center = (16U * 32U + 16U) * 3U;
  EXPECT_GT(rgb[center], 200);
  EXPECT_LT(rgb[center + 1U], 60);
  EXPECT_LT(rgb[center + 2U], 60);
}

TEST(JpegEncoderTest, EncodesOddDimensions) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;

  ASSERT_TRUE(encoder.encodeI420(makeI420Frame(33, 17, 100, 128, 128), jpeg)) << encoder.lastError();

  const auto header = readHeader(jpeg);
  EXPECT_EQ(header.width, 33);
  EXPECT_EQ(header.height, 17);
}

TEST(JpegEncoderTest, RejectsNonI420FrameAndLeavesOutputUnchanged) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg{1, 2, 3};

  EXPECT_FALSE(encoder.encodeI420(livekit::VideoFrame::create(8, 8, livekit::VideoBufferType::RGBA), jpeg));

  EXPECT_EQ(jpeg, (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_FALSE(encoder.lastError().empty());
}

TEST(JpegEncoderTest, RejectsI420BufferWithUnexpectedSize) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;
  // Row padding would make the buffer larger than the packed layout.
  const livekit::VideoFrame padded(8, 8, livekit::VideoBufferType::I420, std::vector<std::uint8_t>(200U, 0U));

  EXPECT_FALSE(encoder.encodeI420(padded, jpeg));
  EXPECT_TRUE(jpeg.empty());
  EXPECT_NE(encoder.lastError().find("does not match"), std::string::npos);
}

TEST(JpegEncoderTest, ReusesOutputStorageForSameSizeFrames) {
  JpegEncoder encoder;
  std::vector<std::uint8_t> jpeg;
  const auto frame = makeI420Frame(64, 64, 128, 128, 128);

  ASSERT_TRUE(encoder.encodeI420(frame, jpeg));
  const auto* first_storage = jpeg.data();
  const auto first_size = jpeg.size();
  ASSERT_TRUE(encoder.encodeI420(frame, jpeg));

  EXPECT_EQ(jpeg.size(), first_size);
  EXPECT_EQ(jpeg.data(), first_storage);
}

} // namespace ros_portal::utils
