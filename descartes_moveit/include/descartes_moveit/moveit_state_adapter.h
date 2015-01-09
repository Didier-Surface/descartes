/*
 * Software License Agreement (Apache License)
 *
 * Copyright (c) 2014, Dan Solomon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MOVEIT_STATE_ADPATER_H_
#define MOVEIT_STATE_ADPATER_H_

#include "descartes_core/robot_model.h"
#include <descartes_core/cart_trajectory_pt.h>
#include "moveit/robot_model/robot_model.h"
#include "moveit/kinematics_base/kinematics_base.h"
#include <string>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_state/robot_state.h>

namespace descartes_moveit
{

/**@brief MoveitStateAdapter adapts the MoveIt RobotState to the Descartes RobotModel interface
 *
 */
class MoveitStateAdapter : public descartes_core::RobotModel
{
public:


  MoveitStateAdapter();

  /**
   * Constructor for Moveit state adapters (implements Descartes robot model interface)
   * @param robot_state robot state object utilized for kinematic/dynamic state checking
   * @param group_name planning group name
   * @param tool_frame tool frame name
   * @param world_frame work object frame name
   */

  MoveitStateAdapter(const moveit::core::RobotState & robot_state, const std::string & group_name,
                    const std::string & tool_frame, const std::string & world_frame,
                     size_t sample_iterations = 10);
  virtual ~MoveitStateAdapter()
  {
  }
  ;

  virtual void initialize(const std::string robot_description, const std::string& group_name,
                          const std::string& world_frame,const std::string& tcp_frame);

  virtual bool getIK(const Eigen::Affine3d &pose, const std::vector<double> &seed_state,
                     std::vector<double> &joint_pose) const;

  virtual bool getAllIK(const Eigen::Affine3d &pose, std::vector<std::vector<double> > &joint_poses) const;

  virtual bool getFK(const std::vector<double> &joint_pose, Eigen::Affine3d &pose) const;

  virtual bool isValid(const std::vector<double> &joint_pose) const;

  virtual bool isValid(const Eigen::Affine3d &pose) const;

  virtual int getDOF() const;

protected:

  /**
   * Gets IK solution (assumes robot state is pre-seeded)
   * @param pose Affine pose of TOOL in WOBJ frame
   * @param joint_pose Solution (if function successful).
   * @return
   */
  bool getIK(const Eigen::Affine3d &pose, std::vector<double> &joint_pose) const;

  /**
   * @brief Pointer to moveit robot state (mutable object state is reset with
   * each function call
   */
  mutable moveit::core::RobotStatePtr robot_state_;
  robot_model_loader::RobotModelLoaderPtr  robot_model_loader_;
  robot_model::RobotModelConstPtr robot_model_ptr_;

  /**
   * @brief Planning group name
   */
  std::string group_name_;

  /**
   * @brief Tool frame name
   */
  std::string tool_frame_;

  /**
   * @brief Work object/reference frame name
   */
  std::string world_frame_;

  /**
   * @brief convenient transformation frame
   */
  descartes_core::Frame world_to_root_;

  /**
   * @brief Joint solution sample iterations for returning "all" joints
   */
  size_t sample_iterations_;

};

} //descartes_moveit

#endif /* MOVEIT_STATE_ADPATER_H_ */
