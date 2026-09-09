#pragma once
#include "algo/fire_smoke/fire_config.h"
#include "cv_sdk/cv_sdk.h"
#include <deque>
#include <map>
#include <opencv2/core.hpp>
#include <vector>
namespace cvsdk {
/**
 * 烟雾时空证据过滤器：过曝/光晕、长基线静态、短基线软运动及锐边屏蔽。
 * 内部维护约 160x90 灰度历史，单路视频独占实例。
 */
class SmokeStaticGate {
public:
  explicit SmokeStaticGate(const FireConfig& config) : config_(config) {}
  /** 输入：当前图像和候选框；输出：原地删除不满足烟雾时空证据的框。 */
  CVSDK_Status Filter(const CVSDK_Image& image, std::vector<CVSDK_Detection>* detections);
  /** 输入：无；输出：清空历史灰度帧和统计计数。 */
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
