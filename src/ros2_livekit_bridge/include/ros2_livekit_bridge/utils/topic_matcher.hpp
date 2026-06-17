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

#ifndef ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_
#define ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_

#include <map>
#include <regex>
#include <string>
#include <vector>

namespace ros2_livekit_bridge::utils
{

struct PatternCompileError
{
  std::string pattern;
  std::string message;
};

//! @brief Empty participants means the route applies to any LiveKit identity.
struct CompiledTopicRoute
{
  std::string pattern;
  std::regex compiled;
  std::vector<std::string> participants;
};

//! @brief Compiled topic routes split by direction, indexed by participant.
struct TopicRouteTable
{
  std::vector<CompiledTopicRoute> outgoing;
  std::vector<CompiledTopicRoute> incoming;
  std::vector<PatternCompileError> errors;
  std::map<std::string, std::vector<std::size_t>> outgoing_by_participant;
  std::map<std::string, std::vector<std::size_t>> incoming_by_participant;
};

std::vector<std::regex> compileRegexPatterns(
  const std::vector<std::string> & patterns,
  std::vector<PatternCompileError> *errors = nullptr);

bool matchesAnyPattern(
  const std::string & value,
  const std::vector<std::regex> & patterns);

bool matchesTopicRoutes(
  const std::string & value,
  const std::vector<CompiledTopicRoute> & routes);

bool matchesTopicRoutesForParticipant(
  const std::string & value,
  const std::string & participant,
  const std::vector<CompiledTopicRoute> & routes,
  const std::map<std::string, std::vector<std::size_t>> & routes_by_participant);

//! @brief Compile one route and index it by participant scope.
void appendCompiledRoute(
  std::vector<CompiledTopicRoute> & routes,
  std::map<std::string, std::vector<std::size_t>> & routes_by_participant,
  const std::string & pattern,
  const std::vector<std::string> & participants,
  std::vector<PatternCompileError> & errors);

//! @brief Append a route to outgoing and/or incoming tables.
void appendTopicRoute(
  TopicRouteTable & route_table,
  const std::string & pattern,
  const std::vector<std::string> & participants,
  bool include_outgoing,
  bool include_incoming);

} // namespace ros2_livekit_bridge::utils

#endif // ROS2_LIVEKIT_BRIDGE__UTILS__TOPIC_MATCHER_HPP_
