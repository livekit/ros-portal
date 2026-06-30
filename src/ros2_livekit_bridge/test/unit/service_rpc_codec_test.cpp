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

#include "ros2_livekit_bridge/service_rpc_codec.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ros2_livekit_bridge {
namespace {

TEST(ServiceRpcCodecTest, RequestRoundTrips) {
  const std::vector<std::uint8_t> cdr{0, 1, 2, 250, 255, 7};
  const auto decoded = decodeServiceRequest(encodeServiceRequest(cdr));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, cdr);
}

TEST(ServiceRpcCodecTest, SuccessResponseRoundTrips) {
  const std::vector<std::uint8_t> cdr{9, 8, 7, 6};
  const auto decoded = decodeServiceResponse(encodeServiceResponse(true, cdr, ""));
  EXPECT_TRUE(decoded.ok);
  EXPECT_EQ(decoded.response_cdr, cdr);
  EXPECT_TRUE(decoded.err.empty());
}

TEST(ServiceRpcCodecTest, FailureResponseCarriesError) {
  const auto decoded = decodeServiceResponse(encodeServiceResponse(false, {}, "boom"));
  EXPECT_FALSE(decoded.ok);
  EXPECT_EQ(decoded.err, "boom");
  EXPECT_TRUE(decoded.response_cdr.empty());
}

TEST(ServiceRpcCodecTest, RejectsMalformedEnvelope) {
  const auto decoded = decodeServiceResponse("this is not json");
  EXPECT_FALSE(decoded.ok);
  EXPECT_FALSE(decoded.err.empty());
}

TEST(ServiceRpcCodecTest, RejectsMissingOkField) {
  const auto decoded = decodeServiceResponse(R"({"resp_b64":"AAAA"})");
  EXPECT_FALSE(decoded.ok);
  EXPECT_FALSE(decoded.err.empty());
}

TEST(ServiceRpcCodecTest, RejectsInvalidBase64Request) { EXPECT_FALSE(decodeServiceRequest("***").has_value()); }

} // namespace
} // namespace ros2_livekit_bridge
