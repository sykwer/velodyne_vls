/*
 *  Copyright (C) 2009, 2010 Austin Robot Technology, Jack O'Quin
 *  Copyright (C) 2011 Jesse Vera
 *  Copyright (C) 2012 Austin Robot Technology, Jack O'Quin
 *  License: Modified BSD Software License Agreement
 *
 *  $Id$
 */

#include <velodyne_pointcloud/interpolate.h>

#include <pcl_conversions/pcl_conversions.h>
#include <velodyne_pointcloud/pointcloudXYZIRADT.h>

#include <velodyne_pointcloud/func.h>

namespace velodyne_pointcloud
{
/** @brief Constructor. */
Interpolate::Interpolate(const rclcpp::NodeOptions & options)
: Node("velodyne_interpolate_node", options),
 tf2_listener_(tf2_buffer_), 
 base_link_frame_("base_link")
{
  // advertise
  velodyne_points_interpolate_pub_ =
    this->create_publisher<sensor_msgs::msg::PointCloud2>("velodyne_points_interpolate", 10);
  velodyne_points_interpolate_ex_pub_ =
    this->create_publisher<sensor_msgs::msg::PointCloud2>("velodyne_points_interpolate_ex", 10);

  // subscribe
  twist_sub_ = this->create_subscription<geometry_msgs::msg::TwistStamped>(
    "/vehicle/status/twist", 10, std::bind(&Interpolate::processTwist, this, std::placeholders::_1));
  velodyne_points_ex_sub_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
    "velodyne_points_ex", 10, std::bind(&Interpolate::processPoints,this, std::placeholders::_1));
}

void Interpolate::processTwist(const geometry_msgs::msg::TwistStamped::SharedPtr twist_msg)
{
  twist_queue_.push_back(*twist_msg);

  while (!twist_queue_.empty()) {
    //for replay rosbag
    if (rclcpp::Time(twist_queue_.front().header.stamp) > rclcpp::Time(twist_msg->header.stamp)) {
      twist_queue_.pop_front();
    } else if (rclcpp::Time(twist_queue_.front().header.stamp) < rclcpp::Time(twist_msg->header.stamp) - rclcpp::Duration::from_seconds(1.0)) {
      twist_queue_.pop_front();
    } else {
      break;
    }
  }
}

void Interpolate::processPoints(
  const sensor_msgs::msg::PointCloud2::SharedPtr points_xyziradt_msg)
{
  if (
    velodyne_points_interpolate_pub_->get_subscription_count() <= 0 &&
    velodyne_points_interpolate_ex_pub_->get_subscription_count() <= 0) {
    return;
  }

  pcl::PointCloud<velodyne_pointcloud::PointXYZIRADT>::Ptr points_xyziradt(
    new pcl::PointCloud<velodyne_pointcloud::PointXYZIRADT>);
  pcl::fromROSMsg(*points_xyziradt_msg, *points_xyziradt);

  pcl::PointCloud<velodyne_pointcloud::PointXYZIRADT>::Ptr interpolate_points_xyziradt(
    new pcl::PointCloud<velodyne_pointcloud::PointXYZIRADT>);
  tf2::Transform tf2_base_link_to_sensor;
  getTransform(points_xyziradt->header.frame_id, base_link_frame_, &tf2_base_link_to_sensor);
  interpolate_points_xyziradt = interpolate(points_xyziradt, twist_queue_, tf2_base_link_to_sensor);

  if (velodyne_points_interpolate_pub_->get_subscription_count() > 0) {
    const auto interpolate_points_xyzir = convert(interpolate_points_xyziradt);
    sensor_msgs::msg::PointCloud2 ros_pc_msg;
    pcl::toROSMsg(*interpolate_points_xyzir, ros_pc_msg);
    velodyne_points_interpolate_pub_->publish(ros_pc_msg);
  }
  if (velodyne_points_interpolate_ex_pub_->get_subscription_count() > 0) {
    sensor_msgs::msg::PointCloud2 ros_pc_msg;
    pcl::toROSMsg(*interpolate_points_xyziradt, ros_pc_msg);
    velodyne_points_interpolate_ex_pub_->publish(ros_pc_msg);
  }
}

bool Interpolate::getTransform(
  const std::string & target_frame, const std::string & source_frame,
  tf2::Transform * tf2_transform_ptr)
{
  if (target_frame == source_frame) {
    tf2_transform_ptr->setOrigin(tf2::Vector3(0, 0, 0));
    tf2_transform_ptr->setRotation(tf2::Quaternion(0, 0, 0, 1));
    return true;
  }

  try {
    const auto transform_msg =
      tf2_buffer_.lookupTransform(target_frame, source_frame, tf2::TimePointZero);
    tf2::convert(transform_msg.transform, *tf2_transform_ptr);
  } catch (tf2::TransformException & ex) {
    RCLCPP_WARN(this->get_logger(), "%s", ex.what());
    RCLCPP_ERROR(this->get_logger(), "Please publish TF %s to %s", target_frame.c_str(), source_frame.c_str());

    tf2_transform_ptr->setOrigin(tf2::Vector3(0, 0, 0));
    tf2_transform_ptr->setRotation(tf2::Quaternion(0, 0, 0, 1));
    return false;
  }
  return true;
}

}  // namespace velodyne_pointcloud

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(velodyne_pointcloud::Interpolate)
