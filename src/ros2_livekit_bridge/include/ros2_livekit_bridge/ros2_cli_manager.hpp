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

#include <rclcpp/rclcpp.hpp>
#include <ros2_livekit_bridge/ros_json_converters.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_interface_show.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_service_list.hpp>
#include <ros2_livekit_bridge_msgs/srv/ros2_topic_list.hpp>

namespace livekit
{
class Room;
}  // namespace livekit

namespace ros2_livekit_bridge
{
/**
 * @brief Minimal RPC transport used by Ros2CliManager.
 *
 * This interface isolates LiveKit-specific calls from ROS CLI request handling
 * so the manager can be unit-tested without connecting to a LiveKit room.
 */
class Ros2CliRpcClient
{
public:
  /**
   * @brief Handler for inbound LiveKit RPC payloads.
   *
   * The input and return value are JSON strings. The concrete transport wraps
   * this callback in the SDK-specific RPC handler signature.
   */
  using RpcHandler = std::function<std::string(const std::string &)>;

  virtual ~Ros2CliRpcClient() = default;

  /**
   * @brief Check whether a remote participant identity is currently present.
   * @param participant_id LiveKit participant identity to look up.
   * @return True when the participant exists in the connected room.
   */
  virtual bool hasParticipant(const std::string & participant_id) const = 0;

  /**
   * @brief Invoke a LiveKit RPC method on a remote participant.
   * @param participant_id LiveKit participant identity to call.
   * @param method LiveKit RPC method name.
   * @param payload JSON request payload.
   * @param timeout_sec Response timeout in seconds.
   * @return JSON response payload returned by the remote handler.
   * @throws livekit::RpcError for LiveKit RPC failures.
   * @throws std::exception for unexpected transport failures.
   */
  virtual std::string performRpc(
    const std::string & participant_id,
    const std::string & method,
    const std::string & payload,
    std::uint8_t timeout_sec) = 0;

  /**
   * @brief Register a local handler for a LiveKit RPC method.
   * @param method LiveKit RPC method name.
   * @param handler Callback that receives a JSON request and returns JSON.
   */
  virtual void registerRpcMethod(
    const std::string & method,
    RpcHandler handler) = 0;

  /**
   * @brief Remove a previously registered local LiveKit RPC method.
   * @param method LiveKit RPC method name to unregister.
   */
  virtual void unregisterRpcMethod(const std::string & method) = 0;
};

/**
 * @brief LiveKit SDK-backed implementation of Ros2CliRpcClient.
 *
 * The adapter uses the room's local participant to register and perform RPC
 * calls, and uses the room's remote participant map for preflight existence
 * checks.
 */
class LiveKitRos2CliRpcClient final : public Ros2CliRpcClient
{
public:
  /**
   * @brief Construct an adapter around an already connected LiveKit room.
   * @param room LiveKit room owned by Ros2LiveKitBridge.
   */
  explicit LiveKitRos2CliRpcClient(livekit::Room & room);

  /**
   * @brief Check whether a remote participant identity is currently present.
   * @param participant_id LiveKit participant identity to look up.
   * @return True when the participant exists in the room.
   */
  bool hasParticipant(const std::string & participant_id) const override;

  /**
   * @brief Invoke a LiveKit RPC method through the local participant.
   * @param participant_id LiveKit participant identity to call.
   * @param method LiveKit RPC method name.
   * @param payload JSON request payload.
   * @param timeout_sec Response timeout in seconds.
   * @return JSON response payload returned by the remote participant.
   */
  std::string performRpc(
    const std::string & participant_id,
    const std::string & method,
    const std::string & payload,
    std::uint8_t timeout_sec) override;

  /**
   * @brief Register a local LiveKit RPC handler on the local participant.
   * @param method LiveKit RPC method name.
   * @param handler Callback that receives and returns JSON strings.
   */
  void registerRpcMethod(
    const std::string & method,
    RpcHandler handler) override;

  /**
   * @brief Unregister a local LiveKit RPC handler from the local participant.
   * @param method LiveKit RPC method name.
   */
  void unregisterRpcMethod(const std::string & method) override;

private:
  livekit::Room & room_;
};

/**
 * @brief Hosts ROS CLI-like introspection services over ROS and LiveKit RPC.
 *
 * Ros2CliManager exposes local ROS services for developers and fulfills remote
 * LiveKit RPCs by querying the local ROS graph. It is intentionally scoped to
 * CLI-style graph introspection so future commands such as interface show or
 * service list can share the same transport and JSON response conventions.
 */
class Ros2CliManager
{
public:
  //! @brief Generated ROS service type for remote `ros2 interface show`.
  using Ros2InterfaceShow = ros2_livekit_bridge_msgs::srv::Ros2InterfaceShow;
  //! @brief Generated ROS service type for remote `ros2 topic list` requests.
  using Ros2TopicList = ros2_livekit_bridge_msgs::srv::Ros2TopicList;
  //! @brief Generated ROS service type for remote `ros2 service list` requests.
  using Ros2ServiceList = ros2_livekit_bridge_msgs::srv::Ros2ServiceList;

  /**
   * @brief Snapshot of one ROS topic used to format `ros2 topic list` output.
   */
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

  /**
   * @brief Snapshot of one ROS service used to format `ros2 service list`.
   */
  struct ServiceInfo
  {
    //! @brief Fully qualified ROS service name.
    std::string name;
    //! @brief ROS interface type names advertised for the service.
    std::vector<std::string> types;
  };

  /**
   * @brief Construct the manager, create the ROS service, and register RPC.
   * @param node Bridge node used for service hosting, graph queries, and logs.
   * @param callback_group Callback group used by the ROS service.
   * @param rpc_client LiveKit RPC transport abstraction.
   * @throws std::invalid_argument when @p rpc_client is null.
   * @throws std::exception when RPC registration fails.
   */
  Ros2CliManager(
    rclcpp::Node & node,
    rclcpp::CallbackGroup::SharedPtr callback_group,
    std::shared_ptr<Ros2CliRpcClient> rpc_client);

  /**
   * @brief Unregister the LiveKit RPC method before destruction.
   */
  ~Ros2CliManager();

  /**
   * @brief Execute a ROS service request by calling a remote LiveKit RPC.
   * @param request ROS service request from the local developer.
   * @return ROS service response with success, err_msg, and topic-list output.
   */
  Ros2TopicList::Response callRemoteTopicList(
    const Ros2TopicList::Request & request) const;

  /**
   * @brief Execute a ROS service request by calling a remote LiveKit RPC.
   * @param request ROS service request from the local developer.
   * @return ROS service response with success, err_msg, and service-list output.
   */
  Ros2ServiceList::Response callRemoteServiceList(
    const Ros2ServiceList::Request & request) const;

  /**
   * @brief Execute a ROS service request by calling a remote LiveKit RPC.
   * @param request ROS service request from the local developer.
   * @return ROS service response with success, err_msg, and interface output.
   */
  Ros2InterfaceShow::Response callRemoteInterfaceShow(
    const Ros2InterfaceShow::Request & request) const;

  /**
   * @brief Fulfill an inbound LiveKit `ros2_topic_list` RPC.
   * @param payload JSON request payload from the remote participant.
   * @return JSON response payload containing success, err_msg, and output.
   */
  std::string handleTopicListRpc(const std::string & payload) const;

  /**
   * @brief Fulfill an inbound LiveKit `ros2_service_list` RPC.
   * @param payload JSON request payload from the remote participant.
   * @return JSON response payload containing success, err_msg, and output.
   */
  std::string handleServiceListRpc(const std::string & payload) const;

  /**
   * @brief Fulfill an inbound LiveKit `ros2_interface_show` RPC.
   * @param payload JSON request payload from the remote participant.
   * @return JSON response payload containing success, err_msg, and output.
   */
  std::string handleInterfaceShowRpc(const std::string & payload) const;

  /**
   * @brief Check whether a topic should be hidden like default ROS2 CLI output.
   * @param topic_name Fully qualified ROS topic name.
   * @return True when any topic token begins with `_`.
   */
  static bool isHiddenTopic(const std::string & topic_name);

  /**
   * @brief Check whether a service should be hidden like default ROS2 CLI output.
   * @param service_name Fully qualified ROS service name.
   * @return True when any service token begins with `_`.
   */
  static bool isHiddenService(const std::string & service_name);

  /**
   * @brief Resolve the user-provided timeout field to an actual timeout.
   * @param timeout_sec Request timeout field; zero means use the default.
   * @return Timeout in seconds to pass to LiveKit RPC.
   */
  static std::uint8_t effectiveTimeout(std::uint8_t timeout_sec);

  /**
   * @brief Format topics using `ros2 topic list` output conventions.
   * @param topics Sorted topic snapshots to render.
   * @param options Topic list formatting options.
   * @return Human-readable topic list output.
   */
  static std::string formatTopicList(
    const std::vector<TopicInfo> & topics,
    const TopicListOptions & options);

  /**
   * @brief Format services using `ros2 service list` output conventions.
   * @param services Sorted service snapshots to render.
   * @param options Service list formatting options.
   * @return Human-readable service list output.
   */
  static std::string formatServiceList(
    const std::vector<ServiceInfo> & services,
    const ServiceListOptions & options);

  /**
   * @brief Render an interface definition using `ros2 interface show` conventions.
   * @param options Interface type and comment handling options.
   * @return Human-readable interface definition.
   * @throws std::exception when the type is invalid or unavailable.
   */
  static std::string renderInterfaceDefinition(
    const InterfaceShowOptions & options);

private:
  static constexpr const char * kTopicListRpcMethod = "ros2_topic_list";
  static constexpr const char * kTopicListServiceName =
    "/ros2_livekit_bridge/ros2_topic_list";
  static constexpr const char * kServiceListRpcMethod = "ros2_service_list";
  static constexpr const char * kServiceListServiceName =
    "/ros2_livekit_bridge/ros2_service_list";
  static constexpr const char * kInterfaceShowRpcMethod = "ros2_interface_show";
  static constexpr const char * kInterfaceShowServiceName =
    "/ros2_livekit_bridge/ros2_interface_show";
  static constexpr std::uint8_t kDefaultTimeoutSec = 10;

  /**
   * @brief Service callback that maps a ROS request into a service response.
   * @param request Shared ROS service request.
   * @param response Shared ROS service response to populate.
   */
  void handleTopicListRosService(
    const std::shared_ptr<Ros2TopicList::Request> request,
    std::shared_ptr<Ros2TopicList::Response> response) const;

  /**
   * @brief Service callback that maps a ROS request into a service response.
   * @param request Shared ROS service request.
   * @param response Shared ROS service response to populate.
   */
  void handleServiceListRosService(
    const std::shared_ptr<Ros2ServiceList::Request> request,
    std::shared_ptr<Ros2ServiceList::Response> response) const;

  /**
   * @brief Service callback that maps a ROS request into a service response.
   * @param request Shared ROS service request.
   * @param response Shared ROS service response to populate.
   */
  void handleInterfaceShowRosService(
    const std::shared_ptr<Ros2InterfaceShow::Request> request,
    std::shared_ptr<Ros2InterfaceShow::Response> response) const;

  /**
   * @brief Capture topics from the local ROS graph.
   * @param options Topic list graph filtering options.
   * @return Sorted topic snapshots.
   */
  std::vector<TopicInfo> collectTopicInfo(const TopicListOptions & options) const;

  /**
   * @brief Capture services from the local ROS graph.
   * @param options Service list graph filtering options.
   * @return Sorted service snapshots.
   */
  std::vector<ServiceInfo> collectServiceInfo(
    const ServiceListOptions & options) const;

  rclcpp::Node & node_;
  std::shared_ptr<Ros2CliRpcClient> rpc_client_;
  rclcpp::Service<Ros2TopicList>::SharedPtr topic_list_service_;
  rclcpp::Service<Ros2ServiceList>::SharedPtr service_list_service_;
  rclcpp::Service<Ros2InterfaceShow>::SharedPtr interface_show_service_;
};

}  // namespace ros2_livekit_bridge
