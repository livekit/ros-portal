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

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "ros_portal/token_loader.hpp"

namespace ros_portal::token {
namespace {

class ScopedEnvironmentVariable {
public:
  explicit ScopedEnvironmentVariable(const char* name) : name_(name) {
    const char* value = std::getenv(name);
    if (value != nullptr) {
      had_value_ = true;
      original_value_ = value;
    }
  }

  ~ScopedEnvironmentVariable() {
    if (had_value_) {
      setenv(name_.c_str(), original_value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  bool had_value_{false};
  std::string original_value_;
};

class TokenLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    unsetenv("LIVEKIT_URL");
    unsetenv("LIVEKIT_TOKEN");
    unsetenv("LIVEKIT_TOKEN_ENDPOINT");
    unsetenv("LIVEKIT_TOKEN_SERVER_ID");
  }

  ScopedEnvironmentVariable url_{"LIVEKIT_URL"};
  ScopedEnvironmentVariable token_{"LIVEKIT_TOKEN"};
  ScopedEnvironmentVariable endpoint_{"LIVEKIT_TOKEN_ENDPOINT"};
  ScopedEnvironmentVariable server_id_{"LIVEKIT_TOKEN_SERVER_ID"};
};

TEST_F(TokenLoaderTest, LoadsLiteralSource) {
  ASSERT_EQ(setenv("LIVEKIT_URL", "ws://127.0.0.1:7880", 1), 0);
  ASSERT_EQ(setenv("LIVEKIT_TOKEN", "participant-token", 1), 0);

  TokenLoader token_loader;

  ASSERT_TRUE(token_loader.valid());
  EXPECT_EQ(token_loader.get().server_url, "ws://127.0.0.1:7880");
  EXPECT_EQ(token_loader.get().participant_token, "participant-token");
}

TEST_F(TokenLoaderTest, RejectsMultipleTokenSources) {
  ASSERT_EQ(setenv("LIVEKIT_URL", "ws://127.0.0.1:7880", 1), 0);
  ASSERT_EQ(setenv("LIVEKIT_TOKEN", "participant-token", 1), 0);
  ASSERT_EQ(setenv("LIVEKIT_TOKEN_ENDPOINT", "https://example.com/token", 1), 0);

  TokenLoader token_loader;

  EXPECT_FALSE(token_loader.valid());
}

TEST_F(TokenLoaderTest, RejectsLiteralSourceWithoutUrl) {
  ASSERT_EQ(setenv("LIVEKIT_TOKEN", "participant-token", 1), 0);

  TokenLoader token_loader;

  EXPECT_FALSE(token_loader.valid());
}

} // namespace
} // namespace ros_portal::token
