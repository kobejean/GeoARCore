#include <stdint.h>
#include <iostream>

#include "g2o/core/factory.h"
#include "g2o/core/optimization_algorithm_factory.h"
// #include "g2o/core/robust_kernel_impl.h"
#include "g2o/types/sba/types_six_dof_expmap.h"

#include "lar/processing/bundle_adjustment.h"

G2O_USE_OPTIMIZATION_LIBRARY(eigen);

namespace g2o {
  G2O_REGISTER_TYPE_GROUP(expmap);
  G2O_REGISTER_TYPE(PARAMS_CAMERAPARAMETERS, CameraParameters);
  G2O_REGISTER_TYPE(VERTEX_SE3:EXPMAP, VertexSE3Expmap);
  G2O_REGISTER_TYPE(EDGE_SE3:EXPMAP, EdgeSE3Expmap);
  G2O_REGISTER_TYPE(EDGE_PROJECT_XYZ2UVD:EXPMAP, EdgeProjectXYZ2UVD);

  G2O_REGISTER_TYPE_GROUP(slam3d);
  G2O_REGISTER_TYPE(VERTEX_TRACKXYZ, VertexPointXYZ);
}

namespace lar {

  BundleAdjustment::BundleAdjustment(std::shared_ptr<Mapper::Data> data) : data(data) {
    optimizer.setVerbose(true);
    std::string solver_name = "lm_fix6_3";
    g2o::OptimizationAlgorithmProperty solver_property;
    auto algorithm = g2o::OptimizationAlgorithmFactory::instance()->construct(solver_name, solver_property);
    optimizer.setAlgorithm(algorithm);
  }

  void BundleAdjustment::construct() {
    _stats = Stats();
    _stats.landmarks = std::vector<size_t>(data->frames.size(),0);
    _stats.usable_landmarks = std::vector<size_t>(data->frames.size(),0);

    // Use frame data to add poses and measurements
    for (size_t frame_id = 0; frame_id < data->frames.size(); frame_id++) {
      // Add camera pose vertex
      Frame const& frame = data->frames[frame_id];
      addPose(frame.extrinsics, frame_id, frame_id == data->frames.size() - 1);

      // Add odometry measurement edge if not first frame
      if (frame_id > 0) {
        addOdometry(frame_id);
      }

      // Add camera intrinsics parameters
      size_t params_id = frame_id+1;
      addIntrinsics(frame.intrinsics, params_id);
    }

    // Add landmarks to graph
    size_t landmark_count = data->map.landmarks.size();
    for (size_t landmark_id = 0; landmark_id < landmark_count; landmark_id++) {
      Landmark &landmark = data->map.landmarks[landmark_id];
      size_t id = landmark_id + data->frames.size();
      _stats.total_usable_landmarks += addLandmark(landmark, id);
      addLandmarkMeasurements(landmark, id);
    }
    
    // Print statistics for debuging purposes
    _stats.print();
  }

  void BundleAdjustment::optimize() {
    optimizer.initializeOptimization();
    optimizer.setVerbose(true);
    optimizer.optimize(50);

    size_t landmark_count = data->map.landmarks.size();
    for (size_t i = 0; i < landmark_count; i++) {
      updateLandmark(i);
    }
  }

  // Private methods

  bool BundleAdjustment::addLandmark(Landmark const &landmark, size_t id) {
    if (landmark.isUseable()) {
      g2o::VertexPointXYZ * vertex = new g2o::VertexPointXYZ();
      vertex->setId(id);
#ifndef LAR_COMPACT_BUILD
      vertex->setMarginalized(!landmark.is_fixed);
      vertex->setFixed(landmark.is_fixed);
#else
      vertex->setMarginalized(true);
#endif
      vertex->setEstimate(landmark.position);
      optimizer.addVertex(vertex);
      return true;
    }
    return false;
  }

  void BundleAdjustment::addPose(Eigen::Matrix4d const &extrinsics, size_t id, bool fixed) {
    Eigen::Matrix3d rot = extrinsics.block<3,3>(0,0);
    // Flipping y and z axis to align with image coordinates and depth direction
    rot(Eigen::indexing::all, 1) = -rot(Eigen::indexing::all, 1);
    rot(Eigen::indexing::all, 2) = -rot(Eigen::indexing::all, 2);
    g2o::SE3Quat pose = g2o::SE3Quat(rot, extrinsics.block<3,1>(0,3)).inverse();

    g2o::VertexSE3Expmap * vertex = new g2o::VertexSE3Expmap();
    vertex->setId(id);
    vertex->setEstimate(pose);
    vertex->setFixed(fixed);
    optimizer.addVertex(vertex);
  }

  void BundleAdjustment::addOdometry(size_t frame_id) {
    g2o::VertexSE3Expmap* v1 = dynamic_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(frame_id-1));
    g2o::VertexSE3Expmap* v2 = dynamic_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(frame_id));
    g2o::SE3Quat pose_change = v2->estimate() * v1->estimate().inverse();

    g2o::EdgeSE3Expmap * e = new g2o::EdgeSE3Expmap();
    e->setVertex(0, v1);
    e->setVertex(1, v2);
    e->setMeasurement(pose_change);
    // TODO: Find a better estimate
    e->information() = Eigen::MatrixXd::Identity(6,6) * 80000000;
    optimizer.addEdge(e);
  }

  void BundleAdjustment::addIntrinsics(Eigen::Matrix3d const &intrinsics, size_t id) {
    Eigen::Vector2d principle_point(intrinsics.block<2,1>(0,2));
    auto * cam_params = new g2o::CameraParameters(intrinsics(0,0), principle_point, 0.);
    cam_params->setId(id);
    if (!optimizer.addParameter(cam_params)) {
      assert(false);
    }
  }

  void BundleAdjustment::addLandmarkMeasurements(const Landmark& landmark, size_t id) {
    for (auto const &obs : landmark.obs) {
      size_t frame_id = obs.frame_id;
      Eigen::Vector3d kp(obs.kpt.pt.x, obs.kpt.pt.y, obs.depth);
      
      if (landmark.isUseable()) {
        g2o::EdgeProjectXYZ2UVD * edge = new g2o::EdgeProjectXYZ2UVD();
        edge->setVertex(0, optimizer.vertex(id));
        edge->setVertex(1, optimizer.vertex(frame_id));
        edge->setMeasurement(kp);
        edge->information() = Eigen::Vector3d(1.,1., obs.depth_confidence).asDiagonal();
        edge->setParameterId(0, frame_id+1);
        // g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
        // rk->setDelta(2.5);
        // edge->setRobustKernel(rk);
        optimizer.addEdge(edge);

        _stats.usable_landmarks[frame_id]++;
      }
      _stats.landmarks[frame_id]++;
    }
  }

  void BundleAdjustment::updateLandmark(size_t landmark_id) {
      Landmark &landmark = data->map.landmarks[landmark_id];
      if (landmark.isUseable()) {
        size_t vertex_id = landmark_id + data->frames.size();
        g2o::VertexPointXYZ* v = dynamic_cast<g2o::VertexPointXYZ*>(optimizer.vertex(vertex_id));
        landmark.position = v->estimate();
      }
  }

  void BundleAdjustment::Stats::print() {
    for (size_t i = 0; i < landmarks.size(); i++) {
      std::cout << "frame landmarks: " << landmarks[i] << std::endl;
      std::cout << "frame usable landmarks: " << usable_landmarks[i] << std::endl;
    }

    std::cout << "total usable landmarks: " << total_usable_landmarks << std::endl;
  }
}