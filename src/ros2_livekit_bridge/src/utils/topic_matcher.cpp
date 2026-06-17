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

namespace ros2_livekit_bridge::utils
{

std::vector<std::regex> compileRegexPatterns(
  const std::vector<std::string> & patterns,
  std::vector<PatternCompileError> *errors)
{
  std::vector<std::regex> compiled_patterns;
  compiled_patterns.reserve(patterns.size());

  for (const auto & pattern : patterns) {
    try {
      compiled_patterns.emplace_back(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error & e) {
      if (errors) {
        errors->push_back(PatternCompileError{pattern, e.what()});
      }
    }
  }

  return compiled_patterns;
}

bool matchesAnyPattern(
  const std::string & value,
  const std::vector<std::regex> & patterns)
{
  for (const auto & pattern : patterns) {
    if (std::regex_match(value, pattern)) {
      return true;
    }
  }
  return false;
}

namespace
{

constexpr const char *kAnyParticipantKey = "";

bool matchesRouteIndices(
  const std::string & value,
  const std::vector<CompiledTopicRoute> & routes,
  const std::vector<std::size_t> & route_indices)
{
  for (const auto route_index : route_indices) {
    if (std::regex_match(value, routes.at(route_index).compiled)) {
      return true;
    }
  }
  return false;
}

} // namespace

bool matchesTopicRoutes(
  const std::string & value,
  const std::vector<CompiledTopicRoute> & routes)
{
  for (const auto & route : routes) {
    if (std::regex_match(value, route.compiled)) {
      return true;
    }
  }
  return false;
}

bool matchesTopicRoutesForParticipant(
  const std::string & value,
  const std::string & participant,
  const std::vector<CompiledTopicRoute> & routes,
  const std::map<std::string, std::vector<std::size_t>> & routes_by_participant)
{
  if (const auto participant_it = routes_by_participant.find(participant);
    participant_it != routes_by_participant.end())
  {
    if (matchesRouteIndices(value, routes, participant_it->second)) {
      return true;
    }
  }

  if (const auto any_participant_it = routes_by_participant.find(kAnyParticipantKey);
    any_participant_it != routes_by_participant.end())
  {
    return matchesRouteIndices(value, routes, any_participant_it->second);
  }

  return false;
}

void appendCompiledRoute(
  std::vector<CompiledTopicRoute> & routes,
  std::map<std::string, std::vector<std::size_t>> & routes_by_participant,
  const std::string & pattern,
  const std::vector<std::string> & participants,
  std::vector<PatternCompileError> & errors)
{
  try {
    const auto route_index = routes.size();
    routes.push_back(
      CompiledTopicRoute{
      pattern,
      std::regex(pattern, std::regex::ECMAScript),
      participants,
    });

    if (participants.empty()) {
      routes_by_participant[kAnyParticipantKey].push_back(route_index);
      return;
    }

    for (const auto & participant : participants) {
      routes_by_participant[participant].push_back(route_index);
    }
  } catch (const std::regex_error & e) {
    errors.push_back(PatternCompileError{pattern, e.what()});
  }
}

void appendTopicRoute(
  TopicRouteTable & route_table,
  const std::string & pattern,
  const std::vector<std::string> & participants,
  bool include_outgoing,
  bool include_incoming)
{
  if (include_outgoing) {
    appendCompiledRoute(
      route_table.outgoing,
      route_table.outgoing_by_participant,
      pattern,
      participants,
      route_table.errors);
  }

  if (include_incoming) {
    appendCompiledRoute(
      route_table.incoming,
      route_table.incoming_by_participant,
      pattern,
      participants,
      route_table.errors);
  }
}

} // namespace ros2_livekit_bridge::utils
