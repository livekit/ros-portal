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

#include "ros_portal/utils/video_frame_metadata.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ros_portal::utils {

TEST(VideoFrameMetadataTest, FrameIdRoundTrips) {
  const auto user_data = encodeFrameIdUserData("camera_optical_frame");

  ASSERT_TRUE(user_data.has_value());
  EXPECT_EQ(user_data->front(), kFrameIdUserDataVersion);
  EXPECT_EQ(decodeFrameIdUserData(*user_data), "camera_optical_frame");
}

TEST(VideoFrameMetadataTest, EmptyFrameIdRoundTrips) {
  const auto user_data = encodeFrameIdUserData("");

  ASSERT_TRUE(user_data.has_value());
  EXPECT_EQ(*user_data, (std::vector<std::uint8_t>{kFrameIdUserDataVersion}));
  EXPECT_EQ(decodeFrameIdUserData(*user_data), "");
}

TEST(VideoFrameMetadataTest, EncodeRejectsFrameIdOverLengthLimit) {
  EXPECT_TRUE(encodeFrameIdUserData(std::string(kMaxFrameIdUserDataLength, 'a')).has_value());
  EXPECT_FALSE(encodeFrameIdUserData(std::string(kMaxFrameIdUserDataLength + 1U, 'a')).has_value());
}

TEST(VideoFrameMetadataTest, DecodeRejectsInvalidUserData) {
  EXPECT_FALSE(decodeFrameIdUserData({}).has_value());
  EXPECT_FALSE(decodeFrameIdUserData({0x02, 'a'}).has_value());

  std::vector<std::uint8_t> oversized(kMaxFrameIdUserDataLength + 2U, 'a');
  oversized.front() = kFrameIdUserDataVersion;
  EXPECT_FALSE(decodeFrameIdUserData(oversized).has_value());
}

} // namespace ros_portal::utils
