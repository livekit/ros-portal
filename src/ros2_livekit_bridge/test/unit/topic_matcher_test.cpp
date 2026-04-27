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

#include "ros2_livekit_bridge/utils/topic_matcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace livekit::ros_bridge::utils
{
namespace
{

TEST(TopicMatcherTest, MatchesAnyCompiledPattern) {
  const auto patterns =
    compileRegexPatterns(std::vector<std::string>{"/camera/.*", "/tf"});

  EXPECT_TRUE(matchesAnyPattern("/camera/image_raw", patterns));
  EXPECT_TRUE(matchesAnyPattern("/tf", patterns));
}

TEST(TopicMatcherTest, RejectsValuesThatDoNotFullyMatch) {
  const auto patterns = compileRegexPatterns(std::vector<std::string>{"/tf"});

  EXPECT_FALSE(matchesAnyPattern("/tf_static", patterns));
}

TEST(TopicMatcherTest, EmptyPatternListDoesNotMatch) {
  EXPECT_FALSE(matchesAnyPattern("/camera/image_raw", {}));
}

TEST(TopicMatcherTest, ReportsInvalidPatternsAndKeepsValidPatterns) {
  std::vector<PatternCompileError> errors;
  const auto patterns =
    compileRegexPatterns(std::vector<std::string>{"/tf", "["}, &errors);

  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors.front().pattern, "[");
  EXPECT_FALSE(errors.front().message.empty());
  EXPECT_TRUE(matchesAnyPattern("/tf", patterns));
}

} // namespace
} // namespace livekit::ros_bridge::utils
