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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <rclcpp/create_service.hpp>
#include <rclcpp/node_interfaces/node_base_interface.hpp>
#include <rclcpp/node_interfaces/node_graph_interface.hpp>
#include <rclcpp/node_interfaces/node_logging_interface.hpp>
#include <rclcpp/node_interfaces/node_services_interface.hpp>
#include <rclcpp/node_interfaces/node_topics_interface.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

#include "ros_portal/cli/service_call.hpp"
#include "ros_portal/cli/topic_pub.hpp"
#include "ros_portal/cli/types.hpp"
#include "ros_portal/diagnostics/diagnostics_fns.hpp"
#include "ros_portal/types.hpp"

#ifdef BUILD_TESTING
#include <gtest/gtest_prod.h>
#endif

namespace ros_portal::cli {
/// @brief Hosts ROS CLI-like introspection services over ROS and LiveKit RPC.
///
/// Manager exposes local ROS services for developers and fulfills remote
/// LiveKit RPCs by querying the local ROS graph. It is intentionally scoped to
/// CLI-style graph introspection so future commands such as interface show or
/// service list can share the same transport and JSON response conventions.
class Manager {
public:
  /// @brief LiveKit methods ROS Portal supplies to the manager.
  ///
  /// This struct isolates LiveKit-specific calls from ROS CLI request handling
  /// so the manager owns no reference to a `livekit::Room` and can be
  /// unit-tested without connecting to a LiveKit room. ROS Portal populates each
  /// callback from its own room and passes the struct in at construction.
  struct LiveKitMethods {
    HasParticipantFn has_participant;
    PerformRpcFn perform_rpc;
    RegisterRpcMethodFn register_rpc_method;
    UnregisterRpcMethodFn unregister_rpc_method;
  };

  /// @brief ROS node interfaces required for service hosting and graph queries.
  ///
  /// Holding these interfaces instead of a full @c rclcpp::Node keeps the
  /// manager decoupled from node lifetime and makes the dependency surface
  /// explicit.
  struct NodeInterfaces {
    //! @brief Node identity and shared RCL handle used when creating services.
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_base;
    //! @brief Service registry used when creating ROS services.
    rclcpp::node_interfaces::NodeServicesInterface::SharedPtr node_services;
    //! @brief Graph APIs used for topic and service discovery.
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph;
    //! @brief Topic APIs used to resolve names and create publishers.
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr node_topics;
    //! @brief Node logger used to create the manager's child logger.
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr node_logging;
  };

  /// @brief Construct the manager, create the ROS services, and register RPC.
  ///
  /// Service creation and RPC registration are best-effort: a failure of either
  /// is logged and recorded (OK when both halves exist, ERROR when either is
  /// missing).
  /// @param node_interfaces Node interfaces for service hosting, graph queries,
  /// and logs.
  /// @param callback_group Callback group used by the ROS service.
  /// @param livekit_methods LiveKit methods supplied by ROS Portal.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register the
  /// cli-manager diagnostic task.
  /// @throws std::invalid_argument when any interface, @p livekit_methods
  /// callback, or @p diagnostics is unset.
  Manager(NodeInterfaces node_interfaces, rclcpp::CallbackGroup::SharedPtr callback_group,
          LiveKitMethods livekit_methods, TopicPublishAllowed topic_publish_allowed,
          diagnostics::DiagnosticsManagerFns diagnostics);

  /// @brief Construct the manager from a ROS Portal node.
  ///
  /// Delegates to the @ref NodeInterfaces constructor after extracting the
  /// required node interfaces from @p node.
  /// @param node ROS Portal node used for service hosting, graph queries, and logs.
  /// @param callback_group Callback group used by the ROS service.
  /// @param livekit_methods LiveKit methods supplied by ROS Portal.
  /// @param diagnostics ROS Portal-owned diagnostics functions used to register the
  /// cli-manager diagnostic task.
  /// @throws std::invalid_argument when any extracted interface, @p
  /// livekit_methods callback, or @p diagnostics is unset.
  Manager(rclcpp::Node& node, rclcpp::CallbackGroup::SharedPtr callback_group, LiveKitMethods livekit_methods,
          TopicPublishAllowed topic_publish_allowed, diagnostics::DiagnosticsManagerFns diagnostics);

  /// @brief Unregister the LiveKit RPC method before destruction.
  ~Manager();

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and topic-list output.
  TopicListSrv::Response callRemoteTopicList(const TopicListSrv::Request& request) const;

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and output.
  TopicPubSrv::Response callRemoteTopicPub(const TopicPubSrv::Request& request) const;

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and service-list
  /// output.
  ServiceListSrv::Response callRemoteServiceList(const ServiceListSrv::Request& request) const;

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and output.
  ServiceCallSrv::Response callRemoteServiceCall(const ServiceCallSrv::Request& request) const;

  /// @brief Execute a ROS service request by calling a remote LiveKit RPC.
  /// @param request ROS service request from the local developer.
  /// @return ROS service response with success, err_msg, and interface output.
  InterfaceShowSrv::Response callRemoteInterfaceShow(const InterfaceShowSrv::Request& request) const;

  /// @brief Fulfill an inbound LiveKit `ros2_topic_list` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleTopicListRpc(const std::string& payload) const;

  /// @brief Fulfill an inbound LiveKit `ros2_topic_pub` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleTopicPubRpc(const std::string& payload) const;

  /// @brief Fulfill an inbound LiveKit `ros2_service_list` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleServiceListRpc(const std::string& payload) const;

  /// @brief Fulfill an inbound LiveKit `ros2_service_call` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleServiceCallRpc(const std::string& payload) const;

  /// @brief Fulfill an inbound LiveKit `ros2_interface_show` RPC.
  /// @param payload JSON request payload from the remote participant.
  /// @return JSON response payload containing success, err_msg, and output.
  std::string handleInterfaceShowRpc(const std::string& payload) const;

  /// @brief Check whether a topic should be hidden like default ROS2 CLI output.
  /// @param topic_name Fully qualified ROS topic name.
  /// @return True when any topic token begins with `_`.
  static bool isHiddenTopic(const std::string& topic_name);

  /// @brief Check whether a service should be hidden like default ROS2 CLI
  /// output.
  /// @param service_name Fully qualified ROS service name.
  /// @return True when any service token begins with `_`.
  static bool isHiddenService(const std::string& service_name);

  /// @brief Resolve the user-provided timeout field to an actual timeout.
  /// @param timeout_sec Request timeout field; zero means use the default.
  /// @return Effective timeout in seconds for the remote operation.
  static std::uint8_t effectiveTimeout(std::uint8_t timeout_sec);

  /// @brief Resolve the LiveKit RPC timeout for remote `ros2 service call`.
  /// @param service_timeout_sec Effective remote ROS service-call timeout.
  /// @return LiveKit RPC timeout in seconds.
  static std::uint8_t serviceCallRpcTimeout(std::uint8_t service_timeout_sec);

private:
#ifdef BUILD_TESTING
  FRIEND_TEST(ManagerDiagnosticsTest, ReportsOkWhenAllCommandPairsRegistered);
  FRIEND_TEST(ManagerDiagnosticsTest, ReportsErrorWhenRpcRegistrationFails);
  FRIEND_TEST(ManagerDiagnosticsTest, RemoteFailureBreakdownCountsFailures);
#endif

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleTopicListRosService(const std::shared_ptr<TopicListSrv::Request> request,
                                 std::shared_ptr<TopicListSrv::Response> response) const;

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleTopicPubRosService(const std::shared_ptr<TopicPubSrv::Request> request,
                                std::shared_ptr<TopicPubSrv::Response> response) const;

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleServiceListRosService(const std::shared_ptr<ServiceListSrv::Request> request,
                                   std::shared_ptr<ServiceListSrv::Response> response) const;

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleServiceCallRosService(const std::shared_ptr<ServiceCallSrv::Request> request,
                                   std::shared_ptr<ServiceCallSrv::Response> response) const;

  /// @brief Service callback that maps a ROS request into a service response.
  /// @param request Shared ROS service request.
  /// @param response Shared ROS service response to populate.
  void handleInterfaceShowRosService(const std::shared_ptr<InterfaceShowSrv::Request> request,
                                     std::shared_ptr<InterfaceShowSrv::Response> response) const;

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
  template <typename ResponseT>
  ResponseT performRemoteRpc(const std::string& participant_id, const char* rpc_method,
                             const std::string& request_payload, std::uint8_t timeout_sec) const;

  /// @brief Populate the cli-manager diagnostic status from creation state.
  ///
  /// Reports OK when every CLI command has both its ROS service and its LiveKit
  /// RPC method registered, and ERROR when either half of any command is
  /// missing. Emits one key/value per command describing which half, if any,
  /// failed.
  /// @param status Diagnostic status wrapper to populate.
  void populateStatus(diagnostic_updater::DiagnosticStatusWrapper& status) const;

  /// @brief Whether @p rpc_method was successfully registered at construction.
  bool rpcRegistered(const std::string& rpc_method) const;

  NodeInterfaces node_interfaces_;
  /// @brief Logger owned by the CLI manager, derived from the ROS node logger.
  rclcpp::Logger logger_;
  LiveKitMethods livekit_methods_;
  /// Use a function rather than a static list to account for a dynamic set of
  /// allowed topics.
  TopicPublishAllowed topic_publish_allowed_;
  std::unique_ptr<TopicPub> topic_publisher_;
  std::unique_ptr<ServiceCall> service_caller_;
  rclcpp::Service<TopicListSrv>::SharedPtr topic_list_service_;
  rclcpp::Service<TopicPubSrv>::SharedPtr topic_pub_service_;
  rclcpp::Service<ServiceListSrv>::SharedPtr service_list_service_;
  rclcpp::Service<ServiceCallSrv>::SharedPtr service_call_service_;
  rclcpp::Service<InterfaceShowSrv>::SharedPtr interface_show_service_;
  /// LiveKit RPC method names successfully registered at construction. Used to
  /// unregister exactly those methods on teardown and to render diagnostics.
  std::vector<std::string> registered_rpc_methods_;
  /// ROS Portal-owned diagnostics functions used to (de)register the cli-manager task.
  diagnostics::DiagnosticsManagerFns diagnostics_;
  /// Count of remote calls rejected because the target participant was absent.
  /// Mutable/atomic: incremented from const request handlers on executor and RPC
  /// threads and read from the diagnostic timer thread.
  mutable std::atomic<std::uint64_t> remote_participant_not_found_{0};
  /// Count of remote RPCs that failed at the LiveKit transport layer.
  mutable std::atomic<std::uint64_t> remote_transport_failures_{0};
  /// Count of remote RPC responses that failed to parse as valid JSON.
  mutable std::atomic<std::uint64_t> remote_malformed_responses_{0};
};

} // namespace ros_portal::cli
