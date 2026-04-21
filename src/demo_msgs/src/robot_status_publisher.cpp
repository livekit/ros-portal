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

#include <chrono>
#include <cmath>
#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <demo_msgs/msg/robot_status.hpp>

class RobotStatusPublisher : public rclcpp::Node {
public:
  RobotStatusPublisher() : rclcpp::Node("robot_status_publisher"), tick_(0) {
    pub_ = this->create_publisher<demo_msgs::msg::RobotStatus>(
        "/robot_status", 10);
    timer_ = this->create_wall_timer(std::chrono::seconds(1),
                                     std::bind(&RobotStatusPublisher::publish, this));
    RCLCPP_INFO(this->get_logger(), "Publishing demo_msgs/msg/RobotStatus on /robot_status at 1 Hz");
  }

private:
  void publish() {
    demo_msgs::msg::RobotStatus msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.robot_id = "livekit-bot-01";
    msg.battery_voltage = 12.6 - 0.01 * (tick_ % 100);
    msg.position = {
        std::sin(tick_ * 0.1) * 5.0,
        std::cos(tick_ * 0.1) * 5.0,
        0.0};
    msg.orientation_rpy = {0.0, 0.0, tick_ * 0.1};
    msg.is_moving = (tick_ % 3 != 0);
    msg.operating_mode = static_cast<uint8_t>(tick_ % 4);
    msg.active_sensors = {"lidar", "imu", "camera"};

    pub_->publish(msg);
    RCLCPP_INFO(this->get_logger(),
                "Published RobotStatus (tick=%lu, battery=%.2fV, moving=%s)",
                static_cast<unsigned long>(tick_), msg.battery_voltage,
                msg.is_moving ? "true" : "false");
    tick_++;
  }

  rclcpp::Publisher<demo_msgs::msg::RobotStatus>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::uint64_t tick_;
};

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RobotStatusPublisher>());
  rclcpp::shutdown();
  return 0;
}
