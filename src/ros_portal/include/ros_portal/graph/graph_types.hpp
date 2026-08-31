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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ros_portal {

/// @brief Snapshot of ROS topic names and their discovered interface types.
using TopicNamesAndTypes = std::map<std::string, std::vector<std::string>>;
/// @brief Immutable shared topic-graph snapshot.
using TopicGraphSnapshot = std::shared_ptr<const TopicNamesAndTypes>;
/// @brief Callback returning a current or cached topic-graph snapshot.
using TopicGraphSnapshotFn = std::function<TopicGraphSnapshot()>;

/// @brief Snapshot of ROS service names and their discovered interface types.
using ServiceNamesAndTypes = std::map<std::string, std::vector<std::string>>;
/// @brief Immutable shared service-graph snapshot.
using ServiceGraphSnapshot = std::shared_ptr<const ServiceNamesAndTypes>;
/// @brief Callback returning a current or cached service-graph snapshot.
using ServiceGraphSnapshotFn = std::function<ServiceGraphSnapshot()>;

} // namespace ros_portal
