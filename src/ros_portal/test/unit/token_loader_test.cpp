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

#include "ros_portal/token_loader.hpp"

#include <gtest/gtest.h>

#include <cstdlib>

#include "test_common.hpp"

namespace ros_portal {
namespace {

using ros_portal::test::ScopedEnvVar;
using ros_portal::test::setEnv;

class TokenLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    unsetenv("LIVEKIT_URL");
    unsetenv("LIVEKIT_TOKEN");
    unsetenv("LIVEKIT_TOKEN_ENDPOINT");
    unsetenv("LIVEKIT_TOKEN_SERVER_ID");
  }

  ScopedEnvVar url_{"LIVEKIT_URL"};
  ScopedEnvVar token_{"LIVEKIT_TOKEN"};
  ScopedEnvVar endpoint_{"LIVEKIT_TOKEN_ENDPOINT"};
  ScopedEnvVar server_id_{"LIVEKIT_TOKEN_SERVER_ID"};
};

TEST_F(TokenLoaderTest, LoadsLiteralSource) {
  ASSERT_TRUE(setEnv("LIVEKIT_URL", "ws://127.0.0.1:7880"));
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", "participant-token"));

  TokenLoader token_loader;

  ASSERT_TRUE(token_loader.valid());
  EXPECT_EQ(token_loader.get().server_url, "ws://127.0.0.1:7880");
  EXPECT_EQ(token_loader.get().participant_token, "participant-token");
}

TEST_F(TokenLoaderTest, RejectsMultipleTokenSources) {
  ASSERT_TRUE(setEnv("LIVEKIT_URL", "ws://127.0.0.1:7880"));
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", "participant-token"));
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN_ENDPOINT", "https://example.com/token"));

  TokenLoader token_loader;

  EXPECT_FALSE(token_loader.valid());
}

TEST_F(TokenLoaderTest, RejectsLiteralSourceWithoutUrl) {
  ASSERT_TRUE(setEnv("LIVEKIT_TOKEN", "participant-token"));

  TokenLoader token_loader;

  EXPECT_FALSE(token_loader.valid());
}

} // namespace
} // namespace ros_portal
