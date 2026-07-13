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

#include <cstdint>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils {
namespace {

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// Known-answer vectors from RFC 4648.
TEST(Base64Test, EncodesKnownVectors) {
  EXPECT_EQ(base64Encode(bytes("")), "");
  EXPECT_EQ(base64Encode(bytes("f")), "Zg==");
  EXPECT_EQ(base64Encode(bytes("fo")), "Zm8=");
  EXPECT_EQ(base64Encode(bytes("foo")), "Zm9v");
  EXPECT_EQ(base64Encode(bytes("foob")), "Zm9vYg==");
  EXPECT_EQ(base64Encode(bytes("fooba")), "Zm9vYmE=");
  EXPECT_EQ(base64Encode(bytes("foobar")), "Zm9vYmFy");
}

TEST(Base64Test, DecodesKnownVectors) {
  EXPECT_EQ(base64Decode("Zg=="), bytes("f"));
  EXPECT_EQ(base64Decode("Zm8="), bytes("fo"));
  EXPECT_EQ(base64Decode("Zm9v"), bytes("foo"));
  EXPECT_EQ(base64Decode("Zm9vYg=="), bytes("foob"));
  EXPECT_EQ(base64Decode("Zm9vYmE="), bytes("fooba"));
  EXPECT_EQ(base64Decode("Zm9vYmFy"), bytes("foobar"));
}

TEST(Base64Test, RoundTripsArbitraryBinary) {
  std::vector<std::uint8_t> data;
  data.reserve(512);
  for (int i = 0; i < 512; ++i) {
    data.push_back(static_cast<std::uint8_t>((i * 37 + 11) & 0xFF));
  }
  const auto decoded = base64Decode(base64Encode(data));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, data);
}

TEST(Base64Test, RoundTripsAllByteValues) {
  std::vector<std::uint8_t> data;
  data.reserve(256);
  for (int i = 0; i < 256; ++i) {
    data.push_back(static_cast<std::uint8_t>(i));
  }
  const auto decoded = base64Decode(base64Encode(data));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, data);
}

TEST(Base64Test, RejectsMalformedInput) {
  EXPECT_FALSE(base64Decode("Zm9").has_value());      // length not a multiple of 4
  EXPECT_FALSE(base64Decode("Zm9*").has_value());     // invalid character
  EXPECT_FALSE(base64Decode("Z===").has_value());     // padding in position 1
  EXPECT_FALSE(base64Decode("Zm==Zm9v").has_value()); // padding before the final quartet
}

TEST(Base64Test, DecodesEmptyString) {
  const auto decoded = base64Decode("");
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->empty());
}

} // namespace
} // namespace ros2_livekit_bridge::utils
