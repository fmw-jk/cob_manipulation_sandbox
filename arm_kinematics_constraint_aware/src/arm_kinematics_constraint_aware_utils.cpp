//Software License Agreement (BSD License)

//Copyright (c) 2008, Willow Garage, Inc.
//All rights reserved.

//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions
//are met:

// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above
//   copyright notice, this list of conditions and the following
//   disclaimer in the documentation and/or other materials provided
//   with the distribution.
// * Neither the name of Willow Garage, Inc. nor the names of its
//   contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.

//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//POSSIBILITY OF SUCH DAMAGE.

#include <arm_kinematics_constraint_aware/arm_kinematics_constraint_aware_utils.h>

namespace arm_kinematics_constraint_aware
{
  static const double IK_DEFAULT_TIMEOUT = 10.0;
  static const int NUM_JOINTS_ARM7DOF = 7;
  static const double IK_EPS = 1e-5;
  bool loadRobotModel(ros::NodeHandle node_handle, urdf::Model &robot_model, std::string &root_name, std::string &tip_name, std::string &xml_string)
  {
    std::string urdf_xml,full_urdf_xml;
    node_handle.param("urdf_xml",urdf_xml,std::string("robot_description"));
    node_handle.searchParam(urdf_xml,full_urdf_xml);
    TiXmlDocument xml;
    ROS_DEBUG("Reading xml file from parameter server\n");
    std::string result;
    if (node_handle.getParam(full_urdf_xml, result))
      xml.Parse(result.c_str());
    else
    {
      ROS_FATAL("Could not load the xml from parameter server: %s\n", urdf_xml.c_str());
      return false;
    }
    xml_string = result;
    TiXmlElement *root_element = xml.RootElement();
    TiXmlElement *root = xml.FirstChildElement("robot");
    if (!root || !root_element)
    {
      ROS_FATAL("Could not parse the xml from %s\n", urdf_xml.c_str());
      exit(1);
    }
    robot_model.initXml(root);
    if (!node_handle.getParam("root_name", root_name)){
      ROS_FATAL("No root name found on parameter server");
      return false;
    }
    if (!node_handle.getParam("tip_name", tip_name)){
      ROS_FATAL("No tip name found on parameter server");
      return false;
    }
    return true;
  }

  bool getKDLChain(const std::string &xml_string, const std::string &root_name, const std::string &tip_name, KDL::Chain &kdl_chain)
  {
    // create robot chain from root to tip
    KDL::Tree tree;
    if (!kdl_parser::treeFromString(xml_string, tree))
    {
      ROS_ERROR("Could not initialize tree object");
      return false;
    }
    if (!tree.getChain(root_name, tip_name, kdl_chain))
    {
      ROS_ERROR("Could not initialize chain object");
      return false;
    }
    return true;
  }

  bool getKDLTree(const std::string &xml_string, const std::string &root_name, const std::string &tip_name, KDL::Tree &kdl_tree)
  {
    // create robot chain from root to tip
    if (!kdl_parser::treeFromString(xml_string, kdl_tree))
    {
      ROS_ERROR("Could not initialize tree object");
      return false;
    }
    return true;
  }


  Eigen::Matrix4f KDLToEigenMatrix(const KDL::Frame &p)
  {
    Eigen::Matrix4f b = Eigen::Matrix4f::Identity();
    for(int i=0; i < 3; i++)
    {
      for(int j=0; j<3; j++)
      {
        b(i,j) = p.M(i,j);
      }
      b(i,3) = p.p(i);
    }
    return b;
  }

  double computeEuclideanDistance(const std::vector<double> &array_1, const KDL::JntArray &array_2)
  {
    double distance = 0.0;
    for(int i=0; i< (int) array_1.size(); i++)
    {
      distance += (array_1[i] - array_2(i))*(array_1[i] - array_2(i));
    }
    return sqrt(distance);
  }


  double distance(const urdf::Pose &transform)
  {
    return sqrt(transform.position.x*transform.position.x+transform.position.y*transform.position.y+transform.position.z*transform.position.z);
  }


  bool solveQuadratic(const double &a, const double &b, const double &c, double *x1, double *x2)
  {
    double discriminant = b*b-4*a*c;
    if(fabs(a) < IK_EPS)
    {
      *x1 = -c/b;
      *x2 = *x1;
      return true;
    }
    //ROS_DEBUG("Discriminant: %f\n",discriminant);
    if (discriminant >= 0)
    {      
      *x1 = (-b + sqrt(discriminant))/(2*a); 
      *x2 = (-b - sqrt(discriminant))/(2*a);
      return true;
    } 
    else if(fabs(discriminant) < IK_EPS)
    {
      *x1 = -b/(2*a);
      *x2 = -b/(2*a);
      return true;
    }
    else
    {
      *x1 = -b/(2*a);
      *x2 = -b/(2*a);
      return false;
    }
  }

  Eigen::Matrix4f matrixInverse(const Eigen::Matrix4f &g)
  {
    Eigen::Matrix4f result = g;
    Eigen::Matrix3f Rt = Eigen::Matrix3f::Identity();

    Eigen::Vector3f p = Eigen::Vector3f::Zero(3);
    Eigen::Vector3f pinv = Eigen::Vector3f::Zero(3);

    Rt(0,0) = g(0,0);
    Rt(1,1) = g(1,1);
    Rt(2,2) = g(2,2);

    Rt(0,1) = g(1,0);
    Rt(1,0) = g(0,1);

    Rt(0,2) = g(2,0);
    Rt(2,0) = g(0,2);

    Rt(1,2) = g(2,1);
    Rt(2,1) = g(1,2);

    p(0) = g(0,3);
    p(1) = g(1,3);
    p(2) = g(2,3);

    pinv = -Rt*p;

    result(0,0) = g(0,0);
    result(1,1) = g(1,1);
    result(2,2) = g(2,2);

    result(0,1) = g(1,0);
    result(1,0) = g(0,1);

    result(0,2) = g(2,0);
    result(2,0) = g(0,2);

    result(1,2) = g(2,1);
    result(2,1) = g(1,2);

    result(0,3) = pinv(0);
    result(1,3) = pinv(1);
    result(2,3) = pinv(2);
  
    return result;
  }


  bool solveCosineEqn(const double &a, const double &b, const double &c, double &soln1, double &soln2)
  {
    double theta1 = atan2(b,a);
    double denom  = sqrt(a*a+b*b);

    if(fabs(denom) < IK_EPS) // should never happen, wouldn't make sense but make sure it is checked nonetheless
    {
#ifdef DEBUG
      std::cout << "denom: " << denom << std::endl;
#endif
      return false;
    }
    double rhs_ratio = c/denom;
    if(rhs_ratio < -1 || rhs_ratio > 1)
    {
#ifdef DEBUG
      std::cout << "rhs_ratio: " << rhs_ratio << std::endl;
#endif
      return false;
    }
    double acos_term = acos(rhs_ratio);
    soln1 = theta1 + acos_term;
    soln2 = theta1 - acos_term;

    return true;
  }

  bool checkJointNames(const std::vector<std::string> &joint_names,
                       const kinematics_msgs::KinematicSolverInfo &chain_info)
  {    
    for(unsigned int i=0; i < chain_info.joint_names.size(); i++)
    {
      int index = -1;
      for(unsigned int j=0; j < joint_names.size(); j++)
      {
        if(chain_info.joint_names[i] == joint_names[j])
        {
          index = j;
          break;
        }
      }
      if(index < 0)
      {
        ROS_ERROR("Joint state does not contain joint state for %s.",chain_info.joint_names[i].c_str());
        return false;
      }
    }
    return true;
  }

  bool checkLinkNames(const std::vector<std::string> &link_names,
                      const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    if(link_names.empty())
      return false;
    for(unsigned int i=0; i < link_names.size(); i++)
    {
      if(!checkLinkName(link_names[i],chain_info))
        return false;
    }
    return true;   
  }

  bool checkLinkName(const std::string &link_name,
                   const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    for(unsigned int i=0; i < chain_info.link_names.size(); i++)
    {
      if(link_name == chain_info.link_names[i])
        return true;
    }
    return false;   
  }

  bool checkRobotState(arm_navigation_msgs::RobotState &robot_state,
                     const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    if((int) robot_state.joint_state.position.size() != (int) robot_state.joint_state.name.size())
    {
      ROS_ERROR("Number of joints in robot_state.joint_state does not match number of positions in robot_state.joint_state");
      return false;
    }    
    if(!checkJointNames(robot_state.joint_state.name,chain_info))
    {
      ROS_ERROR("Robot state must contain joint state for every joint in the kinematic chain");
      return false;
    }
    return true;
  }

  bool checkFKService(kinematics_msgs::GetPositionFK::Request &request, 
                      kinematics_msgs::GetPositionFK::Response &response, 
                      const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    if(!checkLinkNames(request.fk_link_names,chain_info))
    {
      ROS_ERROR("Link name in service request does not match links that kinematics can provide solutions for.");
      response.error_code.val = response.error_code.INVALID_LINK_NAME;
      return false;
    }
    if(!checkRobotState(request.robot_state,chain_info))
    {
      response.error_code.val = response.error_code.INVALID_ROBOT_STATE;
      return false;
    }
    return true;
  }

  bool checkIKService(kinematics_msgs::GetPositionIK::Request &request, 
                      kinematics_msgs::GetPositionIK::Response &response,
                      const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    if(!checkLinkName(request.ik_request.ik_link_name,chain_info))
    {
      ROS_ERROR("Link name in service request does not match links that kinematics can provide solutions for.");      
      response.error_code.val = response.error_code.INVALID_LINK_NAME;
      return false;
    }
    if(!checkRobotState(request.ik_request.ik_seed_state,chain_info))
    {
      response.error_code.val = response.error_code.INVALID_ROBOT_STATE;
      return false;
    }
    if(request.timeout <= ros::Duration(0.0))
      {
        response.error_code.val = response.error_code.INVALID_TIMEOUT;
	return false;
      }
    return true;
  }

  bool checkConstraintAwareIKService(kinematics_msgs::GetConstraintAwarePositionIK::Request &request, 
                                     kinematics_msgs::GetConstraintAwarePositionIK::Response &response,
                                     const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    if(!checkLinkName(request.ik_request.ik_link_name,chain_info))
    {
      ROS_ERROR("Link name in service request does not match links that kinematics can provide solutions for.");      
      response.error_code.val = response.error_code.INVALID_LINK_NAME;
      return false;
    }
    if(!checkRobotState(request.ik_request.ik_seed_state,chain_info))
    {
      response.error_code.val = response.error_code.INVALID_ROBOT_STATE;
      return false;
    }
    if(request.timeout <= ros::Duration(0.0))
      {
        response.error_code.val = response.error_code.INVALID_TIMEOUT;
	return false;
      }
    return true;
  }

  bool convertPoseToRootFrame(const geometry_msgs::PoseStamped &pose_msg, 
                              KDL::Frame &pose_kdl, 
                              const std::string &root_frame, 
                              const tf::TransformListener &tf)
  {
    geometry_msgs::PoseStamped pose_stamped;
    if(!convertPoseToRootFrame(pose_msg, pose_stamped, root_frame,tf))
      return false;
    tf::PoseMsgToKDL(pose_stamped.pose, pose_kdl);
    return true;
  }


  bool convertPoseToRootFrame(const geometry_msgs::PoseStamped &pose_msg, 
                              geometry_msgs::PoseStamped &pose_msg_out, 
                              const std::string &root_frame, 
                              const tf::TransformListener &tf)
  {
    geometry_msgs::PoseStamped pose_msg_in = pose_msg;
    ROS_DEBUG("Request:\nframe_id: %s\nPosition: %f %f %f\n:Orientation: %f %f %f %f\n",
              pose_msg_in.header.frame_id.c_str(),
              pose_msg_in.pose.position.x,
              pose_msg_in.pose.position.y,
              pose_msg_in.pose.position.z,
              pose_msg_in.pose.orientation.x,
              pose_msg_in.pose.orientation.y,
              pose_msg_in.pose.orientation.z,
              pose_msg_in.pose.orientation.w);
    tf::Stamped<tf::Pose> pose_stamped;
    poseStampedMsgToTF(pose_msg_in, pose_stamped);
    
    if (!tf.canTransform(root_frame, pose_stamped.frame_id_, pose_stamped.stamp_))
    {
      std::string err;    
      if (tf.getLatestCommonTime(pose_stamped.frame_id_, root_frame, pose_stamped.stamp_, &err) != tf::NO_ERROR)
      {
        ROS_ERROR("Cannot transform from '%s' to '%s'. TF said: %s",pose_stamped.frame_id_.c_str(),root_frame.c_str(), err.c_str());
        return false;
      }
    }    
    try
    {
      tf.transformPose(root_frame, pose_stamped, pose_stamped);
    }
    catch(...)
    {
      ROS_ERROR("Cannot transform from '%s' to '%s'",pose_stamped.frame_id_.c_str(),root_frame.c_str());
      return false;
    } 
    tf::poseStampedTFToMsg(pose_stamped,pose_msg_out);   
    return true;
  }



  int getJointIndex(const std::string &name,
                    const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    for(unsigned int i=0; i < chain_info.joint_names.size(); i++)
    {
      if(chain_info.joint_names[i] == name)
      {
          return i;
      }
    }
    return -1;
  }

  void getKDLChainInfo(const KDL::Chain &chain,
                       kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    int i=0; // segment number
    while(i < (int)chain.getNrOfSegments())
    {
      chain_info.link_names.push_back(chain.getSegment(i).getName());
      i++;
    }
  }

  int getKDLSegmentIndex(const KDL::Chain &chain, 
                         const std::string &name)
  {
    int i=0; // segment number
    while(i < (int)chain.getNrOfSegments())
    {
      if(chain.getSegment(i).getName() == name)
      {
        return i+1;
      }
      i++;
    }
    return -1;   
  }

  void reorderJointState(sensor_msgs::JointState &joint_state, 
                         const kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    sensor_msgs::JointState  tmp_state = joint_state;
    for(unsigned int i=0; i < joint_state.position.size(); i++)
    {
      int tmp_index = getJointIndex(joint_state.name[i],chain_info);
      if(tmp_index >=0)
      {
        tmp_state.position[tmp_index] = joint_state.position[i];
        tmp_state.name[tmp_index] = joint_state.name[i];
      }
    }
    joint_state = tmp_state;
  }
  //  arm_kinematics_constraint_aware::reorderJointState(request.ik_request.ik_seed_state,chain_info_);

  bool getChainInfo(const std::string &name, 
                    kinematics_msgs::KinematicSolverInfo &chain_info)
  {
    std::string urdf_xml, full_urdf_xml, root_name, tip_name;
    ros::NodeHandle node_handle;

    ros::NodeHandle private_handle("~"+name);
    node_handle.param("urdf_xml",urdf_xml,std::string("robot_description"));
    node_handle.searchParam(urdf_xml,full_urdf_xml);
    ROS_DEBUG("Reading xml file from parameter server");
    std::string result;
    if (!node_handle.getParam(full_urdf_xml, result)) 
    {
      ROS_FATAL("Could not load the xml from parameter server: %s", urdf_xml.c_str());
      return false;
    }
    
    // Get Root and Tip From Parameter Service
    if (!private_handle.getParam("root_name", root_name)) 
    {
      ROS_FATAL("GenericIK: No root name found on parameter server");
      return false;
    }
    if (!private_handle.getParam("tip_name", tip_name)) 
    {
      ROS_FATAL("GenericIK: No tip name found on parameter server");
      return false;
    }
    urdf::Model robot_model;
    KDL::Tree tree;
    if (!robot_model.initString(result)) 
    {
      ROS_FATAL("Could not initialize robot model");
      return -1;
    }
    if (!getChainInfoFromRobotModel(robot_model,root_name,tip_name,chain_info)) 
    {
      ROS_FATAL("Could not read information about the joints");
      return false;
    }
    return true;
  }

  bool getChainInfoFromRobotModel(urdf::Model &robot_model,
                                  const std::string &root_name,
                                  const std::string &tip_name,
                                  kinematics_msgs::KinematicSolverInfo &chain_info) 
  {
    // get joint maxs and mins
    boost::shared_ptr<const urdf::Link> link = robot_model.getLink(tip_name);
    boost::shared_ptr<const urdf::Joint> joint;
    while (link && link->name != root_name) 
    {
      joint = robot_model.getJoint(link->parent_joint->name);
      if (!joint) 
      {
        ROS_ERROR("Could not find joint: %s",link->parent_joint->name.c_str());
        return false;
      }
      if (joint->type != urdf::Joint::UNKNOWN && joint->type != urdf::Joint::FIXED) 
      {
        float lower, upper;
        int hasLimits;
        if ( joint->type != urdf::Joint::CONTINUOUS ) 
        {
          lower = joint->limits->lower;
          upper = joint->limits->upper;
          hasLimits = 1;
        } 
        else 
        {
          lower = -M_PI;
          upper = M_PI;
          hasLimits = 0;
        }
        chain_info.joint_names.push_back(joint->name);
        arm_navigation_msgs::JointLimits limits;
        limits.joint_name = joint->name;
        limits.has_position_limits = hasLimits;
        limits.min_position = lower;
        limits.max_position = upper;
        chain_info.limits.push_back(limits);
      }
      link = robot_model.getLink(link->getParent()->name);
    }
    link = robot_model.getLink(tip_name);
    if(link)
      chain_info.link_names.push_back(tip_name);    

    std::reverse(chain_info.limits.begin(),chain_info.limits.end());
    std::reverse(chain_info.joint_names.begin(),chain_info.joint_names.end());

    return true;
  }

  arm_navigation_msgs::ArmNavigationErrorCodes kinematicsErrorCodeToMotionPlanningErrorCode(const int &kinematics_error_code)
  {
    arm_navigation_msgs::ArmNavigationErrorCodes error_code;

    if(kinematics_error_code == kinematics::SUCCESS)
      error_code.val = error_code.SUCCESS;
    else if(kinematics_error_code == kinematics::TIMED_OUT)
      error_code.val = error_code.TIMED_OUT;
    else if(kinematics_error_code == kinematics::NO_IK_SOLUTION)
      error_code.val = error_code.NO_IK_SOLUTION;
    else if(kinematics_error_code == kinematics::FRAME_TRANSFORM_FAILURE)
      error_code.val = error_code.FRAME_TRANSFORM_FAILURE;
    else if(kinematics_error_code == kinematics::IK_LINK_INVALID)
      error_code.val = error_code.INVALID_LINK_NAME;
    else if(kinematics_error_code == kinematics::IK_LINK_IN_COLLISION)
      error_code.val = error_code.IK_LINK_IN_COLLISION;
    else if(kinematics_error_code == kinematics::STATE_IN_COLLISION)
      error_code.val = error_code.COLLISION_CONSTRAINTS_VIOLATED;
    else if(kinematics_error_code == kinematics::INVALID_LINK_NAME)
      error_code.val = error_code.INVALID_LINK_NAME;
    else if(kinematics_error_code == kinematics::GOAL_CONSTRAINTS_VIOLATED)
      error_code.val = error_code.GOAL_CONSTRAINTS_VIOLATED;
    else if(kinematics_error_code == kinematics::INACTIVE)
      error_code.val = 0;
    return error_code;
  }

}
