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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ros_portal::utils {
namespace {

sensor_msgs::msg::Image makeImage(std::uint32_t width, std::uint32_t height, std::uint32_t step,
                                  const std::string& encoding, std::vector<std::uint8_t> data) {
  sensor_msgs::msg::Image image;
  image.width = width;
  image.height = height;
  image.step = step;
  image.encoding = encoding;
  image.data = std::move(data);
  return image;
}

TEST(ImageFrameBuilderTest, MovesPackedRgb8WithoutCopy) {
  auto image = makeImage(2, 1, 6, "rgb8", {10, 20, 30, 40, 50, 60});
  const auto* pixels = image.data.data();
  ImageFrameBuilder builder;

  const auto* frame = builder.build(image);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::RGB24);
  EXPECT_EQ(frame->data(), pixels);
  EXPECT_TRUE(image.data.empty());
}

TEST(ImageFrameBuilderTest, MapsPackedFourChannelEncodingsToNativeLayouts) {
  ImageFrameBuilder builder;
  auto rgba = makeImage(1, 1, 4, "rgba8", {1, 2, 3, 4});
  const auto* rgba_frame = builder.build(rgba);
  ASSERT_NE(rgba_frame, nullptr);
  EXPECT_EQ(rgba_frame->type(), livekit::VideoBufferType::RGBA);

  auto bgra = makeImage(1, 1, 4, "bgra8", {1, 2, 3, 4});
  const auto* bgra_frame = builder.build(bgra);
  ASSERT_NE(bgra_frame, nullptr);
  EXPECT_EQ(bgra_frame->type(), livekit::VideoBufferType::BGRA);
  EXPECT_EQ(std::vector<std::uint8_t>(bgra_frame->data(), bgra_frame->data() + 4), (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

TEST(ImageFrameBuilderTest, DropsTrailingBytesOnZeroCopyPath) {
  auto image = makeImage(1, 1, 4, "rgba8", {1, 2, 3, 4, 99, 99});
  ImageFrameBuilder builder;

  const auto* frame = builder.build(image);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->dataSize(), 4U);
}

TEST(ImageFrameBuilderTest, CopiesPaddedRgba8Rows) {
  auto image = makeImage(2, 2, 10, "rgba8", {1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 9, 10, 11, 12, 13, 14, 15, 16, 88, 88});
  ImageFrameBuilder builder;

  const auto* frame = builder.build(image);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::RGBA);
  EXPECT_EQ(std::vector<std::uint8_t>(frame->data(), frame->data() + frame->dataSize()),
            (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}));
}

TEST(ImageFrameBuilderTest, ConvertsBgr8ToRgb24) {
  auto image = makeImage(2, 1, 6, "bgr8", {10, 20, 30, 40, 50, 60});
  ImageFrameBuilder builder;

  const auto* frame = builder.build(image);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::RGB24);
  EXPECT_EQ(std::vector<std::uint8_t>(frame->data(), frame->data() + frame->dataSize()),
            (std::vector<std::uint8_t>{30, 20, 10, 60, 50, 40}));
}

TEST(ImageFrameBuilderTest, ConvertsMono8ToI420WithNeutralChroma) {
  // Odd size: chroma planes are 2x2 each.
  auto image = makeImage(3, 3, 4, "mono8", {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0});
  ImageFrameBuilder builder;

  const auto* frame = builder.build(image);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->type(), livekit::VideoBufferType::I420);
  ASSERT_EQ(frame->dataSize(), 9U + 8U);
  EXPECT_EQ(std::vector<std::uint8_t>(frame->data(), frame->data() + 9), (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9}));
  EXPECT_EQ(std::vector<std::uint8_t>(frame->data() + 9, frame->data() + 17), std::vector<std::uint8_t>(8U, 128U));
}

TEST(ImageFrameBuilderTest, ReusesConversionFrameForSameGeometry) {
  ImageFrameBuilder builder;
  auto first = makeImage(2, 1, 6, "bgr8", {10, 20, 30, 40, 50, 60});
  const auto* first_pixels = builder.build(first)->data();

  auto second = makeImage(2, 1, 6, "bgr8", {1, 2, 3, 4, 5, 6});
  const auto* frame = builder.build(second);

  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->data(), first_pixels);
  EXPECT_EQ(frame->data()[0], 3);
}

TEST(ImageFrameBuilderTest, RejectsUnsupportedOrMalformedImages) {
  ImageFrameBuilder builder;
  auto unsupported = makeImage(2, 1, 4, "yuyv", {1, 2, 3, 4});
  EXPECT_EQ(builder.build(unsupported), nullptr);

  // The buffer is shorter than step * height, so reading it would overrun.
  auto short_buffer = makeImage(2, 2, 6, "rgb8", {1, 2, 3, 4, 5, 6});
  EXPECT_EQ(builder.build(short_buffer), nullptr);
  EXPECT_EQ(short_buffer.data.size(), 6U);

  auto narrow_step = makeImage(2, 1, 4, "rgb8", {1, 2, 3, 4, 5, 6});
  EXPECT_EQ(builder.build(narrow_step), nullptr);

  auto empty = makeImage(0, 0, 0, "rgb8", {});
  EXPECT_EQ(builder.build(empty), nullptr);
}

} // namespace
} // namespace ros_portal::utils
