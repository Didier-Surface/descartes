/*
 * Software License Agreement (Apache License)
 *
 * Copyright (c) 2014, Southwest Research Institute
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
/*
 * sparse_planner.cpp
 *
 *  Created on: Dec 17, 2014
 *      Author: ros developer 
 */

#include <descartes_core/sparse_planner.h>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>

namespace descartes_core
{

const int MAX_REPLANNING_ATTEMPTS = 20;
const int INVALID_INDEX = -1;
const double MAX_JOINT_CHANGE = M_PI_4;

SparsePlanner::SparsePlanner(RobotModelConstPtr &model,double sampling):
    PlanningGraph(model),
    sampling_(sampling)
{

}

SparsePlanner::~SparsePlanner()
{

}

void SparsePlanner::setSampling(double sampling)
{
  sampling_ = sampling;
}

bool SparsePlanner::setPoints(const std::vector<TrajectoryPtPtr>& traj)
{
  ros::Time start_time = ros::Time::now();
  cart_points_.assign(traj.begin(),traj.end());
  std::vector<TrajectoryPtPtr> sparse_trajectory_array;
  sampleTrajectory(sampling_,cart_points_,sparse_trajectory_array);
  ROS_INFO_STREAM("Sampled trajectory contains "<<sparse_trajectory_array.size()<<" points from "<<cart_points_.size()<<
                  " points in the dense trajectory");

  if(insertGraph(&sparse_trajectory_array) && plan())
  {
    int planned_count = sparse_solution_array_.size();
    int interp_count = cart_points_.size()  - sparse_solution_array_.size();
    ROS_INFO("Sparse planner succeeded with %i planned point and %i interpolated points in %f seconds",planned_count,interp_count,
             (ros::Time::now() - start_time).toSec());
  }
  else
  {
    return false;
  }


  return true;
}

bool SparsePlanner::addPointAfter(const TrajectoryPt::ID& ref_id,TrajectoryPtPtr cp)
{
  ros::Time start_time = ros::Time::now();
  int sparse_index;
  int index;
  TrajectoryPt::ID prev_id, next_id;

  sparse_index= findNearestSparsePointIndex(ref_id);
  if(sparse_index == INVALID_INDEX)
  {
    ROS_ERROR_STREAM("A point in sparse array near point "<<ref_id<<" could not be found, aborting");
    return false;
  }

  // setting ids from sparse array
  prev_id = std::get<1>(sparse_solution_array_[sparse_index - 1])->getID();
  next_id = std::get<1>(sparse_solution_array_[sparse_index])->getID();

  // inserting into dense array
  index = getDensePointIndex(ref_id);
  if(index == INVALID_INDEX)
  {
    ROS_ERROR_STREAM("Point  "<<ref_id<<" could not be found in dense array, aborting");
    return false;
  }
  auto pos = cart_points_.begin();
  std::advance(pos,index + 1);
  auto new_pos = cart_points_.insert(pos,cp);

  // replanning
  if(addTrajectory(cp,prev_id,next_id) && plan())
  {
    int planned_count = sparse_solution_array_.size();
    int interp_count = cart_points_.size()  - sparse_solution_array_.size();
    ROS_INFO("Sparse planner add operation succeeded, %i planned point and %i interpolated points in %f seconds",
             planned_count,interp_count,(ros::Time::now() -start_time).toSec());
  }
  else
  {
    // recovery     
    cart_points_.erase(new_pos);
    if(removeTrajectory(cp) )
    {

      if(plan())
      {
        ROS_DEBUG_STREAM("Recovery removal of invalid point succeeded");
      }
      else
      {        
        ROS_ERROR_STREAM("Recovery planning after removal of invalid point failed, planner is in a fault state");
      }

    }
    else
    {
      ROS_ERROR_STREAM("Recovery removal of invalid point failed, planner is in a fault state");
    }

    return false;
  }

  return true;
}

bool SparsePlanner::addPointBefore(const TrajectoryPt::ID& ref_id,TrajectoryPtPtr cp)
{
  ros::Time start_time = ros::Time::now();
  int sparse_index;
  int index;
  TrajectoryPt::ID prev_id, next_id;

  sparse_index= findNearestSparsePointIndex(ref_id,false);
  if(sparse_index == INVALID_INDEX)
  {
    ROS_ERROR_STREAM("A point in sparse array near point "<<ref_id<<" could not be found, aborting");
    return false;
  }

  prev_id = (sparse_index == 0) ? boost::uuids::nil_uuid() : std::get<1>(sparse_solution_array_[sparse_index - 1])->getID();
  next_id = std::get<1>(sparse_solution_array_[sparse_index])->getID();

  // inserting into dense array
  index = getDensePointIndex(ref_id);
  if(index == INVALID_INDEX)
  {
    ROS_ERROR_STREAM("Point  "<<ref_id<<" could not be found in dense array, aborting");
    return false;
  }
  auto pos = cart_points_.begin();
  std::advance(pos,index);
  auto new_pos =  cart_points_.insert(pos,cp);

  if(addTrajectory(cp,prev_id,next_id) && plan())
  {
    int planned_count = sparse_solution_array_.size();
    int interp_count = cart_points_.size()  - sparse_solution_array_.size();
    ROS_INFO("Sparse planner add operation succeeded, %i planned point and %i interpolated points in %f seconds",
             planned_count,interp_count,(ros::Time::now() -start_time).toSec());
  }
  else
  {
    // recovery 
    cart_points_.erase(new_pos);
    if(removeTrajectory(cp) )
    {

      if(plan())
      {
        ROS_DEBUG_STREAM("Recovery removal of invalid point succeeded");
      }
      else
      {        
        ROS_ERROR_STREAM("Recovery planning after removal of invalid point failed, planner is in a fault state");
      }

    }
    else
    {
      ROS_ERROR_STREAM("Recovery removal of invalid point failed, planner is in a fault state");
    }
    return false;
  }

  return true;
}

bool SparsePlanner::removePoint(const TrajectoryPt::ID& ref_id)
{
  ros::Time start_time = ros::Time::now();
  int index = getDensePointIndex(ref_id);
  if(index == INVALID_INDEX)
  {
    ROS_ERROR_STREAM("Point  "<<ref_id<<" could not be found in dense array, aborting");
    return false;
  }

  if(isInSparseTrajectory(ref_id))
  {
    if(!removeTrajectory(cart_points_[index]))
    {
      ROS_ERROR_STREAM("Failed to removed point "<<ref_id<<" from sparse trajectory, aborting");
      return false;
    }
  }

  // removing from dense array
  auto pos = cart_points_.begin();
  std::advance(pos,index);
  cart_points_.erase(pos);

  if(plan())
  {
    int planned_count = sparse_solution_array_.size();
    int interp_count = cart_points_.size()  - sparse_solution_array_.size();
    ROS_INFO("Sparse planner remove operation succeeded, %i planned point and %i interpolated points in %f seconds",
             planned_count,interp_count,(ros::Time::now() -start_time).toSec());
  }
  else
  {
    return false;
  }

  return true;
}

bool SparsePlanner::modifyPoint(const TrajectoryPt::ID& ref_id,TrajectoryPtPtr cp)
{
  ros::Time start_time = ros::Time::now();
  int sparse_index;
  TrajectoryPt::ID prev_id, next_id;

  sparse_index= getSparsePointIndex(ref_id);
  cp->setID(ref_id);
  if(sparse_index == INVALID_INDEX)
  {
    sparse_index = findNearestSparsePointIndex(ref_id);
    prev_id = std::get<1>(sparse_solution_array_[sparse_index - 1])->getID();
    next_id = std::get<1>(sparse_solution_array_[sparse_index])->getID();
    if(!addTrajectory(cp,prev_id,next_id))
    {
      ROS_ERROR_STREAM("Failed to add point to sparse trajectory, aborting");
      return false;
    }
  }
  else
  {
    if(!modifyTrajectory(cp))
    {
      ROS_ERROR_STREAM("Failed to modify point in sparse trajectory, aborting");
      return false;
    }
  }

  int index = getDensePointIndex(ref_id);
  cart_points_[index] = cp;
  if( plan())
  {
    int planned_count = sparse_solution_array_.size();
    int interp_count = cart_points_.size()  - sparse_solution_array_.size();
    ROS_INFO("Sparse planner modify operation succeeded, %i planned point and %i interpolated points in %f seconds",
             planned_count,interp_count,(ros::Time::now() -start_time).toSec());
  }
  else
  {
    return false;
  }

  return true;
}

bool SparsePlanner::isInSparseTrajectory(const TrajectoryPt::ID& ref_id)
{
  auto predicate = [&ref_id](std::tuple<int,TrajectoryPtPtr,JointTrajectoryPt>& t)
    {
      return ref_id == std::get<1>(t)->getID();
    };

  return (std::find_if(sparse_solution_array_.begin(),
                       sparse_solution_array_.end(),predicate) != sparse_solution_array_.end());
}

int SparsePlanner::getDensePointIndex(const TrajectoryPt::ID& ref_id)
{
  int index = INVALID_INDEX;
  auto predicate = [&ref_id](TrajectoryPtPtr cp)
    {
      return ref_id == cp->getID();
    };

  auto pos = std::find_if(cart_points_.begin(),cart_points_.end(),predicate);
  if(pos != cart_points_.end())
  {
    index = std::distance(cart_points_.begin(),pos);
  }

  return index;
}

int SparsePlanner::getSparsePointIndex(const TrajectoryPt::ID& ref_id)
{
  int index = INVALID_INDEX;
  auto predicate = [ref_id](std::tuple<int,TrajectoryPtPtr,JointTrajectoryPt>& t)
    {
      return ref_id == std::get<1>(t)->getID();
    };

  auto pos = std::find_if(sparse_solution_array_.begin(),sparse_solution_array_.end(),predicate);
  if(pos != sparse_solution_array_.end())
  {
    index = std::distance(sparse_solution_array_.begin(),pos);
  }

  return index;
}

int SparsePlanner::findNearestSparsePointIndex(const TrajectoryPt::ID& ref_id,bool skip_equal)
{
  int index = INVALID_INDEX;
  int dense_index = getDensePointIndex(ref_id);

  if(dense_index == INVALID_INDEX)
  {
    return index;
  }

  auto predicate = [&dense_index,&skip_equal](std::tuple<int,TrajectoryPtPtr,JointTrajectoryPt>& t)
    {

      if(skip_equal)
      {
        return dense_index < std::get<0>(t);
      }
      else
      {
        return dense_index <= std::get<0>(t);
      }
    };

  auto pos = std::find_if(sparse_solution_array_.begin(),sparse_solution_array_.end(),predicate);
  if(pos != sparse_solution_array_.end())
  {
    index = std::distance(sparse_solution_array_.begin(), pos);
  }
  else
  {
    index = sparse_solution_array_.size()-1; // last
  }

  return index;
}

bool SparsePlanner::getSparseSolutionArray(SolutionArray& sparse_solution_array)
{
  std::list<JointTrajectoryPt> sparse_joint_points;
  std::vector<TrajectoryPtPtr> sparse_cart_points;
  double cost;
  ros::Time start_time = ros::Time::now();
  if(getShortestPath(cost,sparse_joint_points))
  {
    ROS_INFO_STREAM("Sparse solution was found in "<<(ros::Time::now() - start_time).toSec()<<" seconds");
    bool success = getOrderedSparseArray(sparse_cart_points) && (sparse_joint_points.size() == sparse_cart_points.size());
    if(!success)
    {
      ROS_ERROR_STREAM("Failed to retrieve sparse solution due to unequal array sizes, cartetian pts: "<<
                       sparse_cart_points.size()<<", joints pts: "<<sparse_joint_points.size());
      return false;
    }
  }
  else
  {
    ROS_ERROR_STREAM("Failed to find sparse joint solution");
    return false;
  }

  unsigned int i = 0;
  unsigned int index;
  sparse_solution_array.clear();
  sparse_solution_array.reserve(sparse_cart_points.size());
  for(auto& item : sparse_joint_points)
  {
    TrajectoryPtPtr cp = sparse_cart_points[i++];
    JointTrajectoryPt& jp = item;
    index = getDensePointIndex(cp->getID());

    if(index == INVALID_INDEX)
    {
      ROS_ERROR_STREAM("Cartesian point "<<cp->getID()<<" not found");
      return false;
    }
    else
    {
      ROS_DEBUG_STREAM("Point with dense index "<<index<<" and id "<< cp->getID()<< " added to sparse");
    }

    sparse_solution_array.push_back(std::make_tuple(index,cp,jp));
  }
  return true;
}

bool SparsePlanner::getOrderedSparseArray(std::vector<TrajectoryPtPtr>& sparse_array)
{
  const CartesianMap& cart_map = getCartesianMap();
  TrajectoryPt::ID first_id = boost::uuids::nil_uuid();
  auto predicate = [&first_id](const std::pair<TrajectoryPt::ID,CartesianPointInformation>& p)
    {
      const auto& info = p.second;
      if((info.links_.id_previous == boost::uuids::nil_uuid()) && (!p.first.is_nil()))
      {
        first_id = p.first;
        return true;
      }
      else
      {
        return false;
      }
    };

  // finding first point
  if(cart_map.empty()
      || (std::find_if(cart_map.begin(),cart_map.end(),predicate) == cart_map.end())
      || first_id == boost::uuids::nil_uuid())
  {
    return false;
  }

  // copying point pointers in order
  sparse_array.resize(cart_map.size());
  TrajectoryPt::ID current_id = first_id;
  for(int i = 0; i < sparse_array.size(); i++)
  {
    if(cart_map.count(current_id) == 0)
    {
      ROS_ERROR_STREAM("Trajectory point "<<current_id<<" was not found in sparse trajectory.");
      return false;
    }

    const CartesianPointInformation& info  = cart_map.at(current_id);
    sparse_array[i] = info.source_trajectory_;
    current_id = info.links_.id_next;
  }

  return true;
}

bool SparsePlanner::getSolutionJointPoint(const CartTrajectoryPt::ID& cart_id, JointTrajectoryPt& j)
{
  if(joint_points_map_.count(cart_id) > 0)
  {
    j = joint_points_map_[cart_id];
  }
  else
  {
    ROS_ERROR_STREAM("Solution for point "<<cart_id<<" was not found");
    return false;
  }

  return true;
}

void SparsePlanner::sampleTrajectory(double sampling,const std::vector<TrajectoryPtPtr>& dense_trajectory_array,
                      std::vector<TrajectoryPtPtr>& sparse_trajectory_array)
{
  std::stringstream ss;
  int skip = std::ceil(double(1.0f)/sampling);
  ROS_INFO_STREAM("Sampling skip val: "<<skip<< " from sampling val: "<<sampling);
  ss<<"[";
  for(int i = 0; i < dense_trajectory_array.size();i+=skip)
  {
    sparse_trajectory_array.push_back(dense_trajectory_array[i]);
    ss<<i<<" ";
  }
  ss<<"]";
  ROS_INFO_STREAM("Sparse Indices:\n"<<ss.str());

  // add the last one
  if(sparse_trajectory_array.back()->getID() != dense_trajectory_array.back()->getID())
  {
    sparse_trajectory_array.push_back(dense_trajectory_array.back());
  }
}

bool SparsePlanner::interpolateJointPose(const std::vector<double>& start,const std::vector<double>& end,
    double t,std::vector<double>& interp)
{
  if(start.size() != end.size() && (t > 1 || t < 0))
  {
    return false;
  }

  interp.resize(start.size());
  double val = 0.0f;
  for(int i = 0; i < start.size(); i++)
  {
    val = end[i] - (end[i] - start[i]) * (1 - t);
    interp[i] = val;
  }

  return true;
}

bool SparsePlanner::plan()
{

  // solving coarse trajectory
  bool replan = true;
  bool succeeded = false;
  //int max_replanning_attempts = cart_points_.size()/2;
  int max_replanning_attempts = MAX_REPLANNING_ATTEMPTS;
  int replanning_attempts = 0;
  while(replan && getSparseSolutionArray(sparse_solution_array_))
  {
    int sparse_index, point_pos;
    int result = interpolateSparseTrajectory(sparse_solution_array_,sparse_index,point_pos);
    TrajectoryPt::ID prev_id, next_id;
    TrajectoryPtPtr cart_point;
    switch(result)
    {
      case int(InterpolationResult::REPLAN):
          replan = true;
          cart_point = cart_points_[point_pos];

          if(sparse_index == 0)
          {
            prev_id = boost::uuids::nil_uuid();
            next_id = std::get<1>(sparse_solution_array_[sparse_index])->getID();
          }
          else
          {
            prev_id = std::get<1>(sparse_solution_array_[sparse_index-1])->getID();
            next_id = std::get<1>(sparse_solution_array_[sparse_index])->getID();
          }

          if(addTrajectory(cart_point,prev_id,next_id))
          {
            sparse_solution_array_.clear();
            ROS_INFO_STREAM("Added new point to sparse trajectory from dense trajectory at position "<<
                            point_pos<<", re-planning entire trajectory");
          }
          else
          {
            ROS_ERROR_STREAM("Adding point "<<point_pos <<"to sparse trajectory failed, aborting");
            replan = false;
            succeeded = false;
          }

          break;
      case int(InterpolationResult::SUCCESS):
      {
          replan = false;
          succeeded = true;

          // check for large joint changes
          std::vector<double> joints1, joints2;
          for(unsigned int i =  1; i < cart_points_.size() ; i++)
          {
            joints1.clear();
            joints2.clear();
            auto& jp1 = joint_points_map_[cart_points_[i-1]->getID()];
            auto& jp2 = joint_points_map_[cart_points_[i]->getID()];

            jp1.getNominalJointPose(joints1,*robot_model_,joints1);
            jp2.getNominalJointPose(joints2,*robot_model_,joints2);

            if(!checkJointChanges(joints1,joints2,MAX_JOINT_CHANGE))
            {
              replan = false;
              succeeded = false;  
              ROS_ERROR_STREAM("Max allowed joint changes exceeded between points at locations "<< i-1 << " and " << i);
              break;           
            }

          }
      }
          break;
      
      case int(InterpolationResult::ERROR):
          replan = false;
          succeeded = false;
          break;
    }

    if(replanning_attempts++ > max_replanning_attempts)
    {
      ROS_ERROR_STREAM("Maximum number of replanning attempts exceeded, aborting");
      replan = false;
      succeeded = false;
      break;
    }

  }

  return succeeded;
}

bool SparsePlanner::checkJointChanges(const std::vector<double>& s1,
                                      const std::vector<double>& s2, const double& max_change)
{
  if(s1.size()!=s2.size())
  {
    return false;
  }

  for(int i = 0; i < s1.size();i++)
  {
    if(std::abs(s1[i] - s2[i])> max_change)
    {
      return false;
    }
  }

  return true;
}

int SparsePlanner::interpolateSparseTrajectory(const SolutionArray& sparse_solution_array,int &sparse_index, int &point_pos)
{
  // populating full path
  joint_points_map_.clear();
  std::vector<double> start_jpose, end_jpose, rough_interp, aprox_interp, seed_pose(robot_model_->getDOF(),0);
  for(int k = 1; k < sparse_solution_array.size(); k++)
  {
    auto start_index = std::get<0>(sparse_solution_array[k-1]);
    auto end_index = std::get<0>(sparse_solution_array[k]);
    TrajectoryPtPtr start_tpoint = std::get<1>(sparse_solution_array[k-1]);
    TrajectoryPtPtr end_tpoint = std::get<1>(sparse_solution_array[k]);
    const JointTrajectoryPt& start_jpoint = std::get<2>(sparse_solution_array[k-1]);
    const JointTrajectoryPt& end_jpoint = std::get<2>(sparse_solution_array[k]);

    start_jpoint.getNominalJointPose(seed_pose,*robot_model_,start_jpose);
    end_jpoint.getNominalJointPose(seed_pose,*robot_model_,end_jpose);

    // adding start joint point to solution
    joint_points_map_.insert(std::make_pair(start_tpoint->getID(),start_jpoint));

    // interpolating
    int step = end_index - start_index;
    ROS_DEBUG_STREAM("Interpolation parameters: step : "<<step<<", start index "<<start_index<<", end index "<<end_index);
    for(int j = 1; (j < step) && ( (start_index + j) < cart_points_.size()); j++)
    {
      int pos = start_index+j;
      double t = double(j)/double(step);
      if(!interpolateJointPose(start_jpose,end_jpose,t,rough_interp))
      {
        ROS_ERROR_STREAM("Interpolation for point at position "<<pos<< "failed, aborting");
        return (int)InterpolationResult::ERROR;
      }

      TrajectoryPtPtr cart_point = cart_points_[pos];
      if(cart_point->getClosestJointPose(rough_interp,*robot_model_,aprox_interp) )
      {
        if(checkJointChanges(rough_interp,aprox_interp,MAX_JOINT_CHANGE))
        {
          ROS_DEBUG_STREAM("Interpolated point at position "<<pos);
          joint_points_map_.insert(std::make_pair(cart_point->getID(),JointTrajectoryPt(aprox_interp)));
        }
        else
        {
          ROS_WARN_STREAM("Joint changes greater that "<<MAX_JOINT_CHANGE<<" detected for point "<<pos<<
                          ", replanning");
          sparse_index = k;
          point_pos = pos;
          return (int)InterpolationResult::REPLAN;
        }

      }
      else
      {

          ROS_WARN_STREAM("Couldn't find a closest joint pose for point "<< cart_point->getID()<<", replanning");
          sparse_index = k;
          point_pos = pos;
          return (int)InterpolationResult::REPLAN;
      }
    }

    // adding end joint point to solution
    joint_points_map_.insert(std::make_pair(end_tpoint->getID(),end_jpoint));
  }

  return (int)InterpolationResult::SUCCESS;
}

} /* namespace descartes_core */
