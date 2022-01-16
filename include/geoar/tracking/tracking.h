#ifndef GEOAR_TRACKING_H
#define GEOAR_TRACKING_H

#include <opencv2/core.hpp>

#include "geoar/core/map.h"
#include "geoar/tracking/vision.h"

namespace geoar {

  class Tracking {
    public:
      Vision vision;
      Map map;

      Tracking(Map map);
      void localize(cv::InputArray image, cv::Mat intrinsics, cv::Mat dist_coeffs, cv::Mat &rvec, cv::Mat &tvec);
      void localize(cv::InputArray image, cv::Mat intrinsics, cv::Mat &transform);

    private:
      cv::Mat objectPoints(std::vector<cv::DMatch> matches);
      cv::Mat imagePoints(std::vector<cv::DMatch> matches, std::vector<cv::KeyPoint> kpts);
  };

}

#endif /* GEOAR_TRACKING_H */