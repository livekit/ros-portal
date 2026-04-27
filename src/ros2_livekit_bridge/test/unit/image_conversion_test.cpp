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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace livekit::ros_bridge::utils
{
namespace
{

sensor_msgs::msg::Image makeImage(
  std::uint32_t width, std::uint32_t height,
  std::uint32_t step,
  const std::string & encoding,
  std::vector<std::uint8_t> data)
{
  sensor_msgs::msg::Image image;
  image.width = width;
  image.height = height;
  image.step = step;
  image.encoding = encoding;
  image.data = std::move(data);
  return image;
}

TEST(ImageConversionTest, CopiesPackedRgba8) {
  const auto image =
    makeImage(2, 1, 8, "rgba8", {10, 20, 30, 40, 50, 60, 70, 80});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba,
    (std::vector<std::uint8_t>{10, 20, 30, 40, 50, 60, 70, 80}));
}

TEST(ImageConversionTest, CopiesPaddedRgba8Rows) {
  const auto image = makeImage(
      2, 2, 10, "rgba8",
    {1, 2, 3, 4, 5, 6, 7, 8, 99, 99,
      9, 10, 11, 12, 13, 14, 15, 16, 88, 88});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba, (std::vector<std::uint8_t>{
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16}));
}

TEST(ImageConversionTest, ConvertsRgb8ToRgba8) {
  const auto image = makeImage(2, 1, 6, "rgb8", {10, 20, 30, 40, 50, 60});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba, (std::vector<std::uint8_t>{
        10, 20, 30, 255, 40, 50, 60, 255}));
}

TEST(ImageConversionTest, ConvertsBgr8ToRgba8) {
  const auto image = makeImage(2, 1, 6, "bgr8", {10, 20, 30, 40, 50, 60});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba, (std::vector<std::uint8_t>{
        30, 20, 10, 255, 60, 50, 40, 255}));
}

TEST(ImageConversionTest, ConvertsBgra8ToRgba8) {
  const auto image =
    makeImage(2, 1, 8, "bgra8", {10, 20, 30, 40, 50, 60, 70, 80});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba,
    (std::vector<std::uint8_t>{30, 20, 10, 40, 70, 60, 50, 80}));
}

TEST(ImageConversionTest, ConvertsMono8ToRgba8) {
  const auto image = makeImage(2, 1, 2, "mono8", {10, 20});
  std::vector<std::uint8_t> rgba;

  EXPECT_TRUE(convertToRgba(image, rgba));

  EXPECT_EQ(rgba, (std::vector<std::uint8_t>{
        10, 10, 10, 255, 20, 20, 20, 255}));
}

TEST(ImageConversionTest, ReturnsFalseForUnsupportedEncoding) {
  const auto image = makeImage(2, 1, 4, "yuyv", {1, 2, 3, 4});
  std::vector<std::uint8_t> rgba;

  EXPECT_FALSE(convertToRgba(image, rgba));
}

} // namespace
} // namespace livekit::ros_bridge::utils
