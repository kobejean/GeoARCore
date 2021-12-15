#ifndef GEOAR_BUNDLE_ADJUSTMENT_H
#define GEOAR_BUNDLE_ADJUSTMENT_H

#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <nlohmann/json.hpp>
#include <Eigen/Core>

#include "g2o/core/factory.h"
#include "g2o/core/optimization_algorithm_factory.h"
#include "g2o/core/robust_kernel_impl.h"
#include "g2o/core/sparse_optimizer.h"
#include "g2o/solvers/structure_only/structure_only_solver.h"
#include "g2o/stuff/sampler.h"
#include "g2o/types/sba/types_six_dof_expmap.h"
#include "g2o/types/slam3d/types_slam3d.h"

#include "geoar/core/landmark.h"
#include "geoar/core/map.h"
#include "geoar/process/map_processing_data.h"
#include "geoar/process/projection.h"
#include "geoar/process/vision.h"

using namespace Eigen;
using namespace std;
using json = nlohmann::json;

namespace geoar {

  class BundleAdjustment {
    public:
      g2o::SparseOptimizer optimizer;
      MapProcessingData* data;

      BundleAdjustment(MapProcessingData &data);
      void construct(std::string directory);

    private:
      void printStats();
      float angleDifference(float alpha, float beta);
  };

}

#endif /* GEOAR_BUNDLE_ADJUSTMENT_H */