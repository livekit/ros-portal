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

#include "ros_portal/cli/topic_list.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ros_portal/cli/utils.hpp"

namespace ros_portal::cli {

bool isHiddenTopic(const std::string& topic_name) { return hasHiddenNameToken(topic_name); }

std::string formatTopicList(const std::vector<TopicInfo>& topics, const TopicListOptions& options) {
  std::ostringstream stream;

  if (options.count_topics) {
    stream << topics.size() << '\n';
    return stream.str();
  }

  if (!options.verbose) {
    for (const auto& topic : topics) {
      stream << topic.name;
      if (options.show_types) {
        stream << " [" << joinTypes(topic.types) << "]";
      }
      stream << '\n';
    }
    return stream.str();
  }

  stream << "Published topics:\n";
  for (const auto& topic : topics) {
    if (topic.publisher_count == 0) {
      continue;
    }
    stream << " * " << topic.name << " [" << joinTypes(topic.types) << "] " << topic.publisher_count << " publisher";
    if (topic.publisher_count != 1) {
      stream << 's';
    }
    stream << '\n';
  }

  stream << "\nSubscribed topics:\n";
  for (const auto& topic : topics) {
    if (topic.subscriber_count == 0) {
      continue;
    }
    stream << " * " << topic.name << " [" << joinTypes(topic.types) << "] " << topic.subscriber_count << " subscriber";
    if (topic.subscriber_count != 1) {
      stream << 's';
    }
    stream << '\n';
  }

  return stream.str();
}

std::vector<TopicInfo> collectTopicInfo(const TopicNamesAndTypes& topic_names_and_types,
                                        const rclcpp::node_interfaces::NodeGraphInterface& graph,
                                        const TopicListOptions& options) {
  std::vector<TopicInfo> topics;
  topics.reserve(topic_names_and_types.size());

  for (const auto& [topic_name, topic_types] : topic_names_and_types) {
    if (!options.include_hidden_topics && isHiddenTopic(topic_name)) {
      continue;
    }

    TopicInfo topic_info;
    topic_info.name = topic_name;
    topic_info.types = topic_types;
    if (options.verbose) {
      topic_info.publisher_count = graph.count_publishers(topic_name);
      topic_info.subscriber_count = graph.count_subscribers(topic_name);
    }
    topics.push_back(std::move(topic_info));
  }

  std::sort(topics.begin(), topics.end(),
            [](const TopicInfo& lhs, const TopicInfo& rhs) { return lhs.name < rhs.name; });
  return topics;
}

} // namespace ros_portal::cli
