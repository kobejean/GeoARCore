#include <stdint.h>
#include <iostream>

#include "g2o/core/factory.h"
#include "g2o/core/optimization_algorithm_factory.h"
// #include "g2o/core/robust_kernel_impl.h"
#include "g2o/types/sba/types_six_dof_expmap.h"

#include "geoar/process/bundle_adjustment.h"

G2O_USE_OPTIMIZATION_LIBRARY(eigen);

namespace geoar {

  BundleAdjustment::BundleAdjustment(MapProcessingData &data) {
    this->data = &data;
    optimizer.setVerbose(true);
    std::string solver_name = "lm_fix6_3";
    g2o::OptimizationAlgorithmProperty solver_property;
    auto algorithm = g2o::OptimizationAlgorithmFactory::instance()->construct(solver_name, solver_property);
    optimizer.setAlgorithm(algorithm);
  }

  void BundleAdjustment::construct() {

    // Add landmarks to graph
    size_t landmark_count = data->map.landmarks.size();
    for (size_t i = 0; i < landmark_count; i++) {
      Landmark &landmark = data->map.landmarks[i];
      _stats.total_usable_landmarks += addLandmark(landmark, i);
    }

    // Use frame data to add poses and measurements
    size_t frame_id = landmark_count;
    for (size_t i = 0; i < data->frames.size(); i++) {
      // Add camera pose vertex
      Frame const& frame = data->frames[i];
      addPose(frame.pose, frame_id, frame_id == landmark_count);

      // Add odometry measurement edge if not first frame
      if (frame_id > landmark_count) {
        addOdometry(frame_id);
      }

      // Add camera intrinsics parameters
      size_t params_id = i+1;
      addIntrinsics(frame.intrinsics, params_id);
      addLandmarkMeasurements(frame, frame_id, params_id);

      frame_id++;
    }
    
    // Print statistics for debuging purposes
    _stats.print();
  }

  // Private methods

  bool BundleAdjustment::addLandmark(Landmark const &landmark, size_t id) {
    if (landmark.sightings >= 3) {
      g2o::VertexPointXYZ * vertex = new g2o::VertexPointXYZ();
      vertex->setId(id);
      vertex->setMarginalized(true);
      vertex->setEstimate(landmark.position);
      optimizer.addVertex(vertex);
      return true;
    }
    return false;
  }

  void BundleAdjustment::addPose(g2o::SE3Quat const &pose, size_t id, bool fixed) {
    g2o::VertexSE3Expmap * vertex = new g2o::VertexSE3Expmap();
    vertex->setId(id);
    vertex->setEstimate(pose);
    vertex->setFixed(fixed);
    optimizer.addVertex(vertex);
  }

  void BundleAdjustment::addOdometry(size_t last_frame_id) {
    g2o::VertexSE3Expmap* v1 = dynamic_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(last_frame_id-1));
    g2o::VertexSE3Expmap* v2 = dynamic_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(last_frame_id));
    g2o::SE3Quat pose_change = v2->estimate() * v1->estimate().inverse();

    g2o::EdgeSE3Expmap * e = new g2o::EdgeSE3Expmap();
    e->setVertex(0, v1);
    e->setVertex(1, v2);
    e->setMeasurement(pose_change);
    e->information() = Eigen::MatrixXd::Identity(6,6) * 80000000;
    optimizer.addEdge(e);
  }

  void BundleAdjustment::addIntrinsics(nlohmann::json const &intrinsics, size_t id) {
    double focal_length = intrinsics["focalLength"];
    Eigen::Vector2d principle_point(intrinsics["principlePoint"]["x"], intrinsics["principlePoint"]["y"]);

    auto * cam_params = new g2o::CameraParameters(focal_length, principle_point, 0.);
    cam_params->setId(id);
    if (!optimizer.addParameter(cam_params)) {
      assert(false);
    }
  }

  void BundleAdjustment::addLandmarkMeasurements(Frame const &frame, size_t frame_id, size_t params_id) {
    size_t usable_landmarks = 0;

    for (size_t j = 0; j < frame.landmarks.size(); j++) {
      size_t landmark_id = frame.landmarks[j];
      Landmark &landmark = data->map.landmarks[landmark_id];
      cv::KeyPoint keypoint = frame.kpts[j];
      Eigen::Vector3d kp(keypoint.pt.x, keypoint.pt.y, frame.depth[j]);
      
      if (landmark.sightings >= 3) {
        g2o::EdgeProjectXYZ2UVD * edge = new g2o::EdgeProjectXYZ2UVD();
        edge->setVertex(0, optimizer.vertex(landmark_id));
        edge->setVertex(1, optimizer.vertex(frame_id));
        edge->setMeasurement(kp);
        edge->information() = Eigen::Vector3d(1.,1.,frame.confidence[j]).asDiagonal();
        edge->setParameterId(0, params_id);
        // g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
        // rk->setDelta(100.0);
        // e->setRobustKernel(rk);
        optimizer.addEdge(edge);

        usable_landmarks++;
      }
    }

    // Populate stats
    _stats.landmarks.push_back(frame.landmarks.size());
    _stats.usable_landmarks.push_back(usable_landmarks);
  }

  void BundleAdjustment::Stats::print() {
    for (size_t i = 0; i < landmarks.size(); i++) {
      std::cout << "frame landmarks: " << landmarks[i] << std::endl;
      std::cout << "frame usable landmarks: " << usable_landmarks[i] << std::endl;
    }

    std::cout << "total usable landmarks: " << total_usable_landmarks << std::endl;
  }
}