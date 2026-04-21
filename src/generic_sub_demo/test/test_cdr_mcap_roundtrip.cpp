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

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rcutils/types/uint8_array.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_storage/topic_metadata.hpp>

namespace {

sensor_msgs::msg::Imu makeTestImu() {
  sensor_msgs::msg::Imu msg;
  msg.header.stamp.sec = 1234;
  msg.header.stamp.nanosec = 567890;
  msg.header.frame_id = "imu_link";
  msg.orientation.x = 0.0;
  msg.orientation.y = 0.0;
  msg.orientation.z = 0.0;
  msg.orientation.w = 1.0;
  msg.angular_velocity.x = 0.1;
  msg.angular_velocity.y = 0.2;
  msg.angular_velocity.z = 0.3;
  msg.linear_acceleration.x = 0.0;
  msg.linear_acceleration.y = 0.0;
  msg.linear_acceleration.z = 9.81;
  msg.orientation_covariance = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  msg.angular_velocity_covariance = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  msg.linear_acceleration_covariance = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  return msg;
}

std::shared_ptr<rcutils_uint8_array_t>
bytesToRcutils(const std::vector<std::uint8_t> &bytes) {
  auto arr = std::shared_ptr<rcutils_uint8_array_t>(
      new rcutils_uint8_array_t, [](rcutils_uint8_array_t *p) {
        static_cast<void>(rcutils_uint8_array_fini(p));
        delete p;
      });
  *arr = rcutils_get_zero_initialized_uint8_array();
  auto allocator = rcutils_get_default_allocator();
  static_cast<void>(rcutils_uint8_array_init(arr.get(), bytes.size(), &allocator));
  std::memcpy(arr->buffer, bytes.data(), bytes.size());
  arr->buffer_length = bytes.size();
  return arr;
}

} // namespace

class CdrMcapRoundtripTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite() {
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
  }
};

TEST_F(CdrMcapRoundtripTest, GenericSubscriptionCdrMatchesMcap) {
  const std::string topic = "/test_imu";
  const std::string type = "sensor_msgs/msg/Imu";

  // -- 1. Build a deterministic Imu message ---------------------------------
  auto imu = makeTestImu();

  // -- 2. Serialize via rclcpp::Serialization (reference CDR) ---------------
  rclcpp::Serialization<sensor_msgs::msg::Imu> serializer;
  rclcpp::SerializedMessage ref_serialized;
  serializer.serialize_message(&imu, &ref_serialized);

  auto &ref_rcl = ref_serialized.get_rcl_serialized_message();
  std::vector<std::uint8_t> reference_cdr(ref_rcl.buffer,
                                          ref_rcl.buffer + ref_rcl.buffer_length);
  ASSERT_FALSE(reference_cdr.empty()) << "Reference CDR serialization produced 0 bytes";

  // -- 3. Publish and capture via GenericSubscription -----------------------
  auto node = rclcpp::Node::make_shared("cdr_mcap_test_node");
  auto pub =
      node->create_publisher<sensor_msgs::msg::Imu>(topic, rclcpp::QoS(10));

  std::vector<std::uint8_t> generic_cdr;
  std::atomic<bool> received{false};

  auto sub = node->create_generic_subscription(
      topic, type, rclcpp::QoS(10),
      [&](std::shared_ptr<rclcpp::SerializedMessage> msg) {
        auto &raw = msg->get_rcl_serialized_message();
        generic_cdr.assign(raw.buffer, raw.buffer + raw.buffer_length);
        received = true;
      });

  // Allow discovery then publish in a tight loop until received.
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!received && std::chrono::steady_clock::now() < deadline) {
    pub->publish(imu);
    rclcpp::spin_some(node);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  ASSERT_TRUE(received)
      << "GenericSubscription did not receive the message within 5 s";
  ASSERT_FALSE(generic_cdr.empty());

  // -- 4. Write the GenericSubscription bytes into an MCAP via rosbag2 ------
  auto mcap_dir =
      std::filesystem::temp_directory_path() / "generic_sub_cdr_mcap_test";
  std::filesystem::remove_all(mcap_dir);

  {
    rosbag2_cpp::Writer writer;
    rosbag2_storage::StorageOptions opts;
    opts.uri = mcap_dir.string();
    opts.storage_id = "mcap";
    writer.open(opts);

    rosbag2_storage::TopicMetadata meta;
    meta.name = topic;
    meta.type = type;
    meta.serialization_format = "cdr";
    writer.create_topic(meta);

    auto bag_msg =
        std::make_shared<rosbag2_storage::SerializedBagMessage>();
    bag_msg->topic_name = topic;
    bag_msg->recv_timestamp = 1234567890;
    bag_msg->send_timestamp = 1234567890;
    bag_msg->serialized_data = bytesToRcutils(generic_cdr);
    writer.write(bag_msg);
  } // writer closes and flushes here

  // -- 5. Read the MCAP back ------------------------------------------------
  std::vector<std::uint8_t> mcap_cdr;
  {
    rosbag2_cpp::Reader reader;
    rosbag2_storage::StorageOptions opts;
    opts.uri = mcap_dir.string();
    opts.storage_id = "mcap";
    reader.open(opts);

    ASSERT_TRUE(reader.has_next()) << "MCAP file contains no messages";
    auto bag_msg = reader.read_next();
    mcap_cdr.assign(bag_msg->serialized_data->buffer,
                    bag_msg->serialized_data->buffer +
                        bag_msg->serialized_data->buffer_length);
  }

  std::filesystem::remove_all(mcap_dir);

  // -- 6. Assert all three representations are byte-identical ---------------
  EXPECT_EQ(reference_cdr, generic_cdr)
      << "rclcpp::Serialization CDR != GenericSubscription CDR";
  EXPECT_EQ(generic_cdr, mcap_cdr)
      << "GenericSubscription CDR != MCAP round-trip CDR";
  EXPECT_EQ(reference_cdr, mcap_cdr)
      << "rclcpp::Serialization CDR != MCAP round-trip CDR (transitive)";
}
