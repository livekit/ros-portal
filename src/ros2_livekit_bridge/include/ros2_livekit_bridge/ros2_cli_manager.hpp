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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/create_service.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <rclcpp/node_interfaces/node_logging_interface.hpp>
#include <rclcpp/node_interfaces/node_services_interface.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>
#include <rclcpp/rclcpp.hpp>

#include "ros2_livekit_bridge/ros2_cli/ros2_service_call.hpp"
#include "ros2_livekit_bridge/ros2_cli/ros2_topic_pub.hpp"
#include "ros2_livekit_bridge/ros2_cli/types.hpp"
#include "ros2_livekit_bridge/types.hpp"

namespace ros2_livekit_bridge
{
///
/// @brief Hosts ROS CLI-like introspection services over ROS and LiveKit RPC.
///
/// Ros2CliManager exposes local ROS services for developers and fulfills remote
/// LiveKit RPCs by querying the local ROS graph. It is intentionally scoped to
/// CLI-style graph introspection so future commands such as interface show or
/// service list can share the same transport and JSON response conventions.
class Ros2CliManager {
public:
  using Ros2InterfaceShow = ros2_cli::Ros2InterfaceShow;
  using Ros2TopicList = ros2_cli::Ros2TopicList;
  using Ros2TopicPub = ros2_cli::Ros2TopicPub;
  using Ros2TopicPubSrv = ros2_cli::Ros2TopicPubSrv;
  using Ros2ServiceList = ros2_cli::Ros2ServiceList;
  using TopicPublishAllowed = ros2_cli::TopicPublishAllowed;
  using Ros2ServiceCallSrv = ros2_cli::Ros2ServiceCallSrv;

  ///
  /// @brief LiveKit methods the bridge supplies to the manager.
  ///
  /// This struct isolates LiveKit-specific calls from ROS CLI request handling
  /// so the manager owns no reference to a `livekit::Room` and can be
  /// unit-tested without connecting to a LiveKit room. The bridge populates each
  /// callback from its own room and passes the struct in at construction.
  struct LivekitMethods
  {
    HasParticipantFn has_participant;
    PerformRpcFn perform_rpc;
    RegisterRpcMethodFn register_rpc_method;
    UnregisterRpcMethodFn unregister_rpc_method;
  };

  ///
  /// @brief Snapshot of one ROS topic used to format `ros2 topic list` output.
  struct TopicInfo
  {
    //! @brief Fully qualified ROS topic name.
    std::string name;
    //! @brief ROS interface type names advertised for the topic.
    std::vector<std::string> types;
    //! @brief Number of publishers currently discovered for the topic.
    size_t publisher_count{0};
    //! @brief Number of subscribers currently discovered for the topic.
    size_t subscriber_count{0};
  };

  ///
  /// @brief Snapshot of one ROS service used to format `ros2 service list`.
  struct ServiceInfo
  {
    //! @brief Fully qualified ROS service name.
    std::string name;
    //! @brief ROS interface type names advertised for the service.
    std::vector<std::string> types;
  };

  ///
  /// @brief ROS node interfaces required for service hosting and graph queries.
  ///
  /// Holding these interfaces instead of a full @c rclcpp::Node keeps the
  /// manager decoupled from node lifetime and makes the dependency surface
  /// explicit.
  struct NodeInterfaces
  {
    //! @brief Node identity and shared RCL handle used when creating services.
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base;
    //! @brief Service registry used when creating ROS services.
    rclcpp::node_interfaces::NodeServicesInterface::SharedPtr node_services;
    //! @brief Graph APIs used for topic and service discovery.
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph;
    //! @brief Topic APIs used to resolve names and create publishers.
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics;
    //! @brief Logger used for manager diagnostics.
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging;
  };

  ///
  /// @brief Construct the manager, create the ROS service, and register RPC.
  /// @param node_interfaces Node interfaces for service hosting, graph queries,
  /// and logs.
  /// @param callback_group Callback group used by the ROS service.
  /// @param livekit_methods LiveKit methods supplied by the bridge.
  /// @throws std::invalid_argument when any interface or @p livekit_methods
  /// callback is
  /// unset.
  /// @throws std::exception when RPC registration fails.
  Ros2CliManager(
    NodeInterfaces node_interfaces,
    rclcpp::CallbackGroup::SharedPtr callback_group,
    LivekitMethods livekit_methods,
    TopicPublishAllowed topic_publish_allowed = {});

  ///
  /// @brief Construct the manager from a bridge node.
  ///
  /// Delegates to the @ref NodeInterfaces constructor after extracting the
  /// required node interfaces from @p node.
  /// @param node Bridge node used for service hosting, graph queries, and logs.
  /// @param callback_group Callback group used by the ROS service.
  /// @param livekit_methods LiveKit methods supplied by the bridge.
  /// @throws std::invalid_argument when any extracted interface or @p
  /// livekit_methods callback is unset.
  /// @throws std::exception when RPC registration fails.
  Ros2CliManager(
    rclcpp::Node & node,
    rclcpp::CallbackGroup::SharedPtr callback_group,
    LivekitMethods livekit_methods,
    TopicPublishAllowed topic_publish_allowed = {});

  ///
  /// @brief Unregister the LiveKit RPC method before destruction.
  ~Ros2CliManager();

  ///
  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and topic-list output.
  Ros2TopicList::Response
  callRemoteTopicList(const Ros2TopicList::Request & request) const;

  ///
  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and output.
  Ros2TopicPubSrv::Response
  callRemoteTopicPub(const Ros2TopicPubSrv::Request & request) const;

  ///
  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and service-list
  /// output.
  Ros2ServiceList::Response
  callRemoteServiceList(const Ros2ServiceList::Request & request) const;

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and output.
  Ros2ServiceCallSrv::Response
  callRemoteServiceCall(const Ros2ServiceCallSrv::Request & request) const;

  ///
  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and interface output.
  Ros2InterfaceShow::Response
  callRemoteInterfaceShow(const Ros2InterfaceShow::Request & request) const;

  ///
  /// @brief Fulfill an inbound LiveKit `ros2_topic_list` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleTopicListRpc(const std::string & payload) const;

  ///
  /// @brief Fulfill an inbound LiveKit `ros2_topic_pub` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleTopicPubRpc(const std::string & payload) const;

  ///
  /// @brief Fulfill an inbound LiveKit `ros2_service_list` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleServiceListRpc(const std::string & payload) const;

  /// @brief Fulfill an inbound LiveKit `ros2_service_call` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleServiceCallRpc(const std::string & payload) const;

  ///
  /// @brief Fulfill an inbound LiveKit `ros2_interface_show` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleInterfaceShowRpc(const std::string & payload) const;

  ///
  /// @brief Check whether a topic should be hidden like default ROS2 CLI output.
  /// @param topic_name Fully qualified ROS topic name.
  /// @return True when any topic token begins with `_`.
  static bool isHiddenTopic(const std::string & topic_name);

  ///
  /// @brief Check whether a service should be hidden like default ROS2 CLI
  /// output.
  /// @param service_name Fully qualified ROS service name.
  /// @return True when any service token begins with `_`.
  static bool isHiddenService(const std::string & service_name);

  ///
  /// @brief Resolve the user-provided timeout field to an actual timeout.
  /// @param timeout_sec Request timeout field; zero means use the default.
  /// @return Effective timeout in seconds for the remote operation.
  static std::uint8_t effectiveTimeout(std::uint8_t timeout_sec);

  ///
  /// @brief Resolve the LiveKit RPC timeout for remote `ros2 service call`.
  /// @param service_timeout_sec Effective remote ROS service-call timeout.
  /// @return LiveKit RPC timeout in seconds.
  static std::uint8_t serviceCallRpcTimeout(std::uint8_t service_timeout_sec);

private:
  ///
  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleTopicListRosService(
    const std::shared_ptr<Ros2TopicList::Request> request,
    std::shared_ptr<Ros2TopicList::Response> response) const;

  ///
  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleTopicPubRosService(
    const std::shared_ptr<Ros2TopicPubSrv::Request> request,
    std::shared_ptr<Ros2TopicPubSrv::Response> response) const;

  ///
  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleServiceListRosService(
    const std::shared_ptr<Ros2ServiceList::Request> request,
    std::shared_ptr<Ros2ServiceList::Response> response) const;

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleServiceCallRosService(
    const std::shared_ptr<Ros2ServiceCallSrv::Request> request,
    std::shared_ptr<Ros2ServiceCallSrv::Response> response) const;

  ///
  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleInterfaceShowRosService(
    const std::shared_ptr<Ros2InterfaceShow::Request> request,
    std::shared_ptr<Ros2InterfaceShow::Response> response) const;

  ///
  /// @brief Perform one LiveKit RPC and parse its JSON response.
  ///
  /// Shared tail for every callRemote* method: dispatches @p request_payload to
  /// the remote participant, logs and returns an error response on transport
  /// failure or malformed JSON, and otherwise returns the parsed response.
  /// @tparam ResponseT Generated ROS service Response type for the command.
  /// @param participant_id Target LiveKit participant.
  /// @param rpc_method RPC method name to invoke.
  /// @param request_payload JSON request payload.
  /// @param timeout_sec Effective timeout in seconds.
  /// @return Parsed remote response, or an error response on failure.
  template<typename ResponseT>
  ResponseT performRemoteRpc(
    const std::string & participant_id,
    const char * rpc_method,
    const std::string & request_payload,
    std::uint8_t timeout_sec) const;

  NodeInterfaces node_interfaces_;
  LivekitMethods livekit_methods_;
  /// Use a function rather than a static list to account for a dynamic set of
  /// allowed topics.
  TopicPublishAllowed topic_publish_allowed_;
  std::unique_ptr<ros2_cli::Ros2TopicPub> topic_publisher_;
  std::unique_ptr<ros2_cli::Ros2ServiceCall> service_caller_;
  rclcpp::Service<Ros2TopicList>::SharedPtr topic_list_service_;
  rclcpp::Service<Ros2TopicPubSrv>::SharedPtr topic_pub_service_;
  rclcpp::Service<Ros2ServiceList>::SharedPtr service_list_service_;
  rclcpp::Service<Ros2ServiceCallSrv>::SharedPtr service_call_service_;
  rclcpp::Service<Ros2InterfaceShow>::SharedPtr interface_show_service_;
};

} // namespace ros2_livekit_bridge
