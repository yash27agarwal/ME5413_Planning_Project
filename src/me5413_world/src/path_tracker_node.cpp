/** path_tracker_node.cpp
 * 
 * Copyright (C) 2024 Shuo SUN & Advanced Robotics Center, National University of Singapore
 * 
 * MIT License
 * 
 * ROS Node for robot to track a given path
 */

#include "me5413_world/path_tracker_node.hpp"

namespace me5413_world 
{

// Dynamic Parameters
double MAX_THROTTLE;
double THROTTLE_GAIN;
double ROBOT_LENGTH;
bool DEFAULT_LOOKAHEAD_DISTANCE;

void dynamicParamCallback(me5413_world::path_trackerConfig& config, uint32_t level)
{

  MAX_THROTTLE = config.max_throttle;
  THROTTLE_GAIN = config.throttle_gain;
  ROBOT_LENGTH = config.robot_length;
  DEFAULT_LOOKAHEAD_DISTANCE = config.lookahead_distance;
};

PathTrackerNode::PathTrackerNode() : tf2_listener_(tf2_buffer_)
{
  f = boost::bind(&dynamicParamCallback, _1, _2);
  server.setCallback(f);

  this->sub_robot_odom_ = nh_.subscribe("/gazebo/ground_truth/state", 1, &PathTrackerNode::robotOdomCallback, this);
  this->sub_local_path_ = nh_.subscribe("/me5413_world/planning/local_path", 1, &PathTrackerNode::localPathCallback, this);
  this->pub_cmd_vel_ = nh_.advertise<geometry_msgs::Twist>("/jackal_velocity_controller/cmd_vel", 1);

  // Initialization
  this->robot_frame_ = "base_link";
  this->world_frame_ = "world";
};

void PathTrackerNode::localPathCallback(const nav_msgs::Path::ConstPtr& path)
{
  // Calculate absolute errors (wrt to world frame)
  this->pose_world_goal_ = path->poses[11].pose;
  this->pub_cmd_vel_.publish(computeControlOutputs(this->odom_world_robot_, this->pose_world_goal_));

  return;
};

void PathTrackerNode::robotOdomCallback(const nav_msgs::Odometry::ConstPtr& odom)
{
  this->world_frame_ = odom->header.frame_id;
  this->robot_frame_ = odom->child_frame_id;
  this->odom_world_robot_ = *odom.get();

  return;
};

geometry_msgs::Twist PathTrackerNode::computeControlOutputs(const nav_msgs::Odometry& odom_robot, const geometry_msgs::Pose& pose_goal)
{
  //Implement Pure Pursuit Controller for Throttle
  double distance_to_goal = computeDistance(odom_robot.pose.pose.position, pose_goal.position);
  ROS_INFO("distance to goal: %f", distance_to_goal);
  double throttle = computeThrottle(distance_to_goal);

  //Implement Pure Pursuit Controller for Steering
  double steering = computeSteering(odom_robot, pose_goal);

  geometry_msgs::Twist cmd_vel;
  cmd_vel.linear.x = throttle;
  cmd_vel.angular.z = steering;

  // std::cout << "robot velocity is " << velocity << " throttle is " << cmd_vel.linear.x << std::endl;
  // std::cout << "lateral error is " << lat_error << " heading_error is " << heading_error << " steering is " << cmd_vel.angular.z << std::endl;

  return cmd_vel;
}

double PathTrackerNode::computeDistance(const geometry_msgs::Point& p1, const geometry_msgs::Point& p2)
{
  double dx = p2.x - p1.x;
  double dy = p2.y - p1.y;
  return std::sqrt(dx * dx + dy * dy);
}

double PathTrackerNode::computeThrottle(double distance_to_goal)
{
  //adjust throttle based on distance to goal
  return std::min(MAX_THROTTLE, distance_to_goal * THROTTLE_GAIN);
}

double PathTrackerNode::computeSteering(const nav_msgs::Odometry& odom_robot, const geometry_msgs::Pose& pose_goal)
{
  // Compute heading error
  double yaw_robot = tf2::getYaw(odom_robot.pose.pose.orientation);
  double yaw_goal = tf2::getYaw(pose_goal.orientation);
  double heading_error = yaw_goal - yaw_robot;

  // Compute lateral error
  tf2::Vector3 point_robot, point_goal;
  tf2::fromMsg(odom_robot.pose.pose.position, point_robot);
  tf2::fromMsg(pose_goal.position, point_goal);
  double dx = point_goal.getX() - point_robot.getX();
  double dy = point_goal.getY() - point_robot.getY();
  double cross_track_error = std::sqrt(dx * dx + dy * dy);

  // Compute alpha (angle between the goal point and the path point)
  double alpha = std::atan2(dy, dx) - yaw_robot;

  // Ensure alpha is within the range [-pi/2, pi/2]
  if (alpha > M_PI / 2) {
    alpha -= M_PI;
  } else if (alpha < -M_PI / 2) {
    alpha += M_PI;
  }

  // Compute lookahead distance
  double lookahead_distance = computeLookaheadDistance(odom_robot);

  // Compute steering angle using the pure pursuit formula
  double steering = std::atan((2.0 * ROBOT_LENGTH * cross_track_error) / lookahead_distance) + alpha;

  // Limit the steering angle to a maximum value
  const double MAX_STEERING_ANGLE = 0.5;
  if (steering > MAX_STEERING_ANGLE) {
    steering = MAX_STEERING_ANGLE;
  } else if (steering < -MAX_STEERING_ANGLE) {
    steering = -MAX_STEERING_ANGLE;
  }

  // Debug output
  ROS_INFO("Yaw Robot: %f", yaw_robot);
  ROS_INFO("Yaw Goal: %f", yaw_goal);
  ROS_INFO("Heading Error: %f", heading_error);
  ROS_INFO("Cross-track Error: %f", cross_track_error);
  ROS_INFO("Alpha: %f", alpha);
  ROS_INFO("Lookahead Distance: %f", lookahead_distance);
  ROS_INFO("Computed Steering Angle: %f", steering);
  ROS_INFO("Throttle: %f", computeThrottle(computeDistance(odom_robot.pose.pose.position, pose_goal.position)));

  return steering;
}

double PathTrackerNode::computeLookaheadDistance(const nav_msgs::Odometry& odom_robot)
{
  double velocity = odom_robot.twist.twist.linear.x;
  return std::max(1.0, velocity * DEFAULT_LOOKAHEAD_DISTANCE);
}

} // namespace me5413_world

int main(int argc, char** argv)
{
  ros::init(argc, argv, "path_tracker_node");
  me5413_world::PathTrackerNode path_tracker_node;
  ros::spin();  // spin the ros node.
  return 0;
}