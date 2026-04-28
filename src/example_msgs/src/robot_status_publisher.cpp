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
#include <example_msgs/msg/robot_status.hpp>
#include <sensor_msgs/msg/battery_state.hpp>

class RobotStatusPublisher : public rclcpp::Node {
public:
  RobotStatusPublisher()
  : rclcpp::Node("robot_status_publisher"), tick_(0)
  {
    status_pub_ = this->create_publisher<example_msgs::msg::RobotStatus>(
        "/robot_status", 10);
    battery_pub_ = this->create_publisher<sensor_msgs::msg::BatteryState>(
        "/battery_state", 10);
    timer_ = this->create_wall_timer(std::chrono::seconds(1),
                                     std::bind(&RobotStatusPublisher::publish, this));
    RCLCPP_INFO(this->get_logger(),
                "Publishing on /robot_status and /battery_state at 1 Hz");
  }

private:
  void publish()
  {
    const auto now = this->now();
    const double voltage = 12.6 - 0.01 * (tick_ % 100);

    example_msgs::msg::RobotStatus status;
    status.header.stamp = now;
    status.header.frame_id = "base_link";
    status.robot_id = "livekit-bot-01";
    status.battery_voltage = voltage;
    status.position = {
      std::sin(tick_ * 0.1) * 5.0,
      std::cos(tick_ * 0.1) * 5.0,
      0.0};
    status.orientation_rpy = {0.0, 0.0, tick_ * 0.1};
    status.is_moving = (tick_ % 3 != 0);
    status.operating_mode = static_cast<uint8_t>(tick_ % 4);
    status.active_sensors = {"lidar", "imu", "camera"};
    status_pub_->publish(status);

    sensor_msgs::msg::BatteryState battery;
    battery.header.stamp = now;
    battery.header.frame_id = "battery_link";
    battery.voltage = static_cast<float>(voltage);
    battery.current = -1.5f + 0.1f * static_cast<float>(tick_ % 20);
    battery.charge = 4.0f - 0.005f * static_cast<float>(tick_ % 200);
    battery.capacity = 5.0f;
    battery.design_capacity = 5.0f;
    battery.percentage = battery.charge / battery.capacity;
    battery.power_supply_status =
      sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING;
    battery.power_supply_health =
      sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_GOOD;
    battery.power_supply_technology =
      sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_LION;
    battery.present = true;
    battery_pub_->publish(battery);

    RCLCPP_INFO(this->get_logger(),
                "tick=%lu | RobotStatus(%.2fV, moving=%s) + BatteryState(%.1f%%)",
                static_cast<unsigned long>(tick_), voltage,
                status.is_moving ? "true" : "false",
                static_cast<double>(battery.percentage * 100.0f));
    tick_++;
  }

  rclcpp::Publisher<example_msgs::msg::RobotStatus>::SharedPtr status_pub_;
  rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr battery_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::uint64_t tick_;
};

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<RobotStatusPublisher>());
  rclcpp::shutdown();
  return 0;
}
