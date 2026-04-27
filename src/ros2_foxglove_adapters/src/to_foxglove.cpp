#include "ros2_foxglove_adapters/to_foxglove.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include <builtin_interfaces/msg/time.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/quaternion.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <sensor_msgs/msg/point_field.hpp>

#include <google/protobuf/timestamp.pb.h>

namespace ros2_foxglove_adapters
{

namespace
{

void setTimestamp(
  const builtin_interfaces::msg::Time & stamp,
  google::protobuf::Timestamp *out)
{
  out->set_seconds(stamp.sec);
  out->set_nanos(static_cast<int32_t>(stamp.nanosec));
}

void setVector3(const geometry_msgs::msg::Vector3 & in, foxglove::Vector3 *out)
{
  out->set_x(in.x);
  out->set_y(in.y);
  out->set_z(in.z);
}

void setPoint(const geometry_msgs::msg::Point & in, foxglove::Vector3 *out)
{
  out->set_x(in.x);
  out->set_y(in.y);
  out->set_z(in.z);
}

void setQuaternion(
  const geometry_msgs::msg::Quaternion & in,
  foxglove::Quaternion *out)
{
  out->set_x(in.x);
  out->set_y(in.y);
  out->set_z(in.z);
  out->set_w(in.w);
}

void setPose(const geometry_msgs::msg::Pose & in, foxglove::Pose *out)
{
  setPoint(in.position, out->mutable_position());
  setQuaternion(in.orientation, out->mutable_orientation());
}

foxglove::Log makeLog(
  const builtin_interfaces::msg::Time & stamp,
  std::string message,
  foxglove::Log_Level level = foxglove::Log_Level_INFO)
{
  foxglove::Log out;
  setTimestamp(stamp, out.mutable_timestamp());
  out.set_level(level);
  out.set_message(std::move(message));
  return out;
}

foxglove::PackedElementField_NumericType pointFieldTypeToFoxglove(
  uint8_t datatype)
{
  switch (datatype) {
    case sensor_msgs::msg::PointField::INT8:
      return foxglove::PackedElementField_NumericType_INT8;
    case sensor_msgs::msg::PointField::UINT8:
      return foxglove::PackedElementField_NumericType_UINT8;
    case sensor_msgs::msg::PointField::INT16:
      return foxglove::PackedElementField_NumericType_INT16;
    case sensor_msgs::msg::PointField::UINT16:
      return foxglove::PackedElementField_NumericType_UINT16;
    case sensor_msgs::msg::PointField::INT32:
      return foxglove::PackedElementField_NumericType_INT32;
    case sensor_msgs::msg::PointField::UINT32:
      return foxglove::PackedElementField_NumericType_UINT32;
    case sensor_msgs::msg::PointField::FLOAT32:
      return foxglove::PackedElementField_NumericType_FLOAT32;
    case sensor_msgs::msg::PointField::FLOAT64:
      return foxglove::PackedElementField_NumericType_FLOAT64;
    default:
      return foxglove::PackedElementField_NumericType_UNKNOWN;
  }
}

std::string toFixedString(float value, int precision = 3)
{
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value > 0.0f ? "inf" : "-inf";
  }

  std::ostringstream ss;
  ss << std::fixed << std::setprecision(precision) << value;
  return ss.str();
}

} // namespace

foxglove::PoseInFrame toFoxglove(const nav_msgs::msg::Odometry & msg)
{
  foxglove::PoseInFrame out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);
  setPose(msg.pose.pose, out.mutable_pose());
  return out;
}

foxglove::PosesInFrame toFoxglove(const nav_msgs::msg::Path & msg)
{
  foxglove::PosesInFrame out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);

  for (const auto & pose_stamped : msg.poses) {
    setPose(pose_stamped.pose, out.add_poses());
  }

  return out;
}

foxglove::Grid toFoxglove(const nav_msgs::msg::OccupancyGrid & msg)
{
  foxglove::Grid out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);
  setPose(msg.info.origin, out.mutable_pose());
  out.set_column_count(msg.info.width);
  out.mutable_cell_size()->set_x(msg.info.resolution);
  out.mutable_cell_size()->set_y(msg.info.resolution);
  out.set_row_stride(msg.info.width);
  out.set_cell_stride(1);

  auto *field = out.add_fields();
  field->set_name("data");
  field->set_offset(0);
  field->set_type(foxglove::PackedElementField_NumericType_INT8);

  if (!msg.data.empty()) {
    out.set_data(reinterpret_cast<const char *>(msg.data.data()), msg.data.size());
  }

  return out;
}

foxglove::FrameTransform
toFoxglove(const geometry_msgs::msg::TransformStamped & msg)
{
  foxglove::FrameTransform out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_parent_frame_id(msg.header.frame_id);
  out.set_child_frame_id(msg.child_frame_id);
  setVector3(msg.transform.translation, out.mutable_translation());
  setQuaternion(msg.transform.rotation, out.mutable_rotation());
  return out;
}

foxglove::Pose toFoxglove(const geometry_msgs::msg::Pose2D & msg)
{
  foxglove::Pose out;
  out.mutable_position()->set_x(msg.x);
  out.mutable_position()->set_y(msg.y);
  out.mutable_position()->set_z(0.0);

  const double half_theta = msg.theta * 0.5;
  out.mutable_orientation()->set_x(0.0);
  out.mutable_orientation()->set_y(0.0);
  out.mutable_orientation()->set_z(std::sin(half_theta));
  out.mutable_orientation()->set_w(std::cos(half_theta));
  return out;
}

foxglove::Log toFoxglove(const geometry_msgs::msg::PolygonStamped & msg)
{
  std::ostringstream ss;
  ss << "polygon frame=" << msg.header.frame_id << " points=[";
  for (size_t i = 0; i < msg.polygon.points.size(); ++i) {
    const auto & pt = msg.polygon.points[i];
    if (i > 0) {
      ss << ", ";
    }
    ss << "(" << toFixedString(pt.x) << ", " << toFixedString(pt.y) << ", "
       << toFixedString(pt.z) << ")";
  }
  ss << "]";
  return makeLog(msg.header.stamp, ss.str());
}

foxglove::PoseInFrame
toFoxglove(const geometry_msgs::msg::PoseWithCovarianceStamped & msg)
{
  foxglove::PoseInFrame out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);
  setPose(msg.pose.pose, out.mutable_pose());
  return out;
}

foxglove::PointCloud toFoxglove(const sensor_msgs::msg::PointCloud2 & msg)
{
  foxglove::PointCloud out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);
  out.mutable_pose()->mutable_orientation()->set_w(1.0);
  out.set_point_stride(msg.point_step);

  for (const auto & field_in : msg.fields) {
    auto *field_out = out.add_fields();
    field_out->set_name(field_in.name);
    field_out->set_offset(field_in.offset);
    field_out->set_type(pointFieldTypeToFoxglove(field_in.datatype));
  }

  if (!msg.data.empty()) {
    out.set_data(reinterpret_cast<const char *>(msg.data.data()), msg.data.size());
  }

  return out;
}

foxglove::PoseInFrame toFoxglove(const sensor_msgs::msg::Imu & msg)
{
  foxglove::PoseInFrame out;
  setTimestamp(msg.header.stamp, out.mutable_timestamp());
  out.set_frame_id(msg.header.frame_id);
  out.mutable_pose()->mutable_orientation()->set_x(msg.orientation.x);
  out.mutable_pose()->mutable_orientation()->set_y(msg.orientation.y);
  out.mutable_pose()->mutable_orientation()->set_z(msg.orientation.z);
  out.mutable_pose()->mutable_orientation()->set_w(msg.orientation.w);
  return out;
}

foxglove::Log toFoxglove(const sensor_msgs::msg::Joy & msg)
{
  std::ostringstream ss;
  ss << "joy axes=[";
  for (size_t i = 0; i < msg.axes.size(); ++i) {
    if (i > 0) {
      ss << ", ";
    }
    ss << toFixedString(msg.axes[i]);
  }
  ss << "] buttons=[";
  for (size_t i = 0; i < msg.buttons.size(); ++i) {
    if (i > 0) {
      ss << ", ";
    }
    ss << msg.buttons[i];
  }
  ss << "]";
  return makeLog(msg.header.stamp, ss.str());
}

foxglove::Log toFoxglove(const sensor_msgs::msg::BatteryState & msg)
{
  std::ostringstream ss;
  ss << "battery voltage=" << toFixedString(msg.voltage)
     << " current=" << toFixedString(msg.current)
     << " percentage=" << toFixedString(msg.percentage)
     << " charge=" << toFixedString(msg.charge)
     << " capacity=" << toFixedString(msg.capacity)
     << " design_capacity=" << toFixedString(msg.design_capacity)
     << " power_supply_status=" << static_cast<int>(msg.power_supply_status);
  return makeLog(msg.header.stamp, ss.str());
}

foxglove::Log toFoxglove(const std_msgs::msg::String & msg)
{
  return makeLog(builtin_interfaces::msg::Time{}, msg.data);
}

} // namespace ros2_foxglove_adapters
