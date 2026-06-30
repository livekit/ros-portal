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

#include "ros2_livekit_bridge/utils/base64.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils {
namespace {

TEST(Base64Test, RoundTripsAllResidueLengths) {
  for (std::size_t length = 0; length <= 64; ++length) {
    std::vector<std::uint8_t> data(length);
    for (std::size_t i = 0; i < length; ++i) {
      data[i] = static_cast<std::uint8_t>((i * 37U + 11U) & 0xFFU);
    }
    const std::string encoded = base64Encode(data);
    EXPECT_EQ(encoded.size() % 4U, 0U) << "length=" << length;
    const auto decoded = base64Decode(encoded);
    ASSERT_TRUE(decoded.has_value()) << "length=" << length;
    EXPECT_EQ(*decoded, data) << "length=" << length;
  }
}

TEST(Base64Test, EmptyRoundTrips) {
  EXPECT_EQ(base64Encode({}), "");
  const auto decoded = base64Decode("");
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->empty());
}

TEST(Base64Test, MatchesKnownVectors) {
  const std::vector<std::uint8_t> man{'M', 'a', 'n'};
  const std::vector<std::uint8_t> ma{'M', 'a'};
  const std::vector<std::uint8_t> m{'M'};
  EXPECT_EQ(base64Encode(man), "TWFu");
  EXPECT_EQ(base64Encode(ma), "TWE=");
  EXPECT_EQ(base64Encode(m), "TQ==");
}

TEST(Base64Test, RejectsNonMultipleOfFourLength) { EXPECT_FALSE(base64Decode("TWF").has_value()); }

TEST(Base64Test, RejectsInvalidCharacters) { EXPECT_FALSE(base64Decode("****").has_value()); }

TEST(Base64Test, RejectsMisplacedPadding) {
  EXPECT_FALSE(base64Decode("TW=u").has_value());
  EXPECT_FALSE(base64Decode("=AAA").has_value());
}

} // namespace
} // namespace ros2_livekit_bridge::utils
