#pragma once
#include "algo/fire_smoke/fire_config.h"
#include "cv_sdk/cv_sdk.h"
#include <deque>
#include <map>
#include <opencv2/core.hpp>
#include <vector>
namespace cvsdk {
class SmokeStaticGate {
public:
  explicit SmokeStaticGate(const FireConfig& config) : config_(config) {}
  CVSDK_Status Filter(const CVSDK_Image& image, std::vector<CVSDK_Detection>* detections);
  void Reset();

private:
  cv::Mat ToSmallGray(const CVSDK_Image& image) const;
  cv::Mat Region(const cv::Mat& matrix, const CVSDK_Detection& det, int width, int height) const;
  FireConfig config_;
  std::deque<std::pair<double, cv::Mat>> history_;
  cv::Mat last_gray_;
  double last_log_ = 0;
  std::map<std::string, uint64_t> stats_;
};
} // namespace cvsdk
