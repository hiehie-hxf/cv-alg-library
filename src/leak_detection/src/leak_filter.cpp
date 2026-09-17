#include "leak_filter.h"

#include <cstddef>
#include <numeric>

#include <opencv2/imgproc.hpp>

namespace leak {
namespace {

// 掩码在 y 方向的面积加权质心。空掩码或全零掩码返回 0。
double MaskCentroidY(const cv::Mat& mask) {
  if (mask.empty() || mask.type() != CV_8UC1) {
    return 0.0;
  }
  const cv::Moments m = cv::moments(mask, true);
  if (m.m00 <= 0.0) {
    return 0.0;
  }
  return m.m01 / m.m00;
}

}  // namespace

LeakFilter::LeakFilter() = default;

LeakFilter::LeakFilter(const LeakConfig& config) : config_(config) {}

LeakFilter::~LeakFilter() = default;

void LeakFilter::Configure(const LeakConfig& config) {
  config_ = config;
}

LeakAlertState LeakFilter::Process(const std::vector<LeakItem>& items) {
  LeakAlertState state;

  if (config_.window <= 0) {
    return state;
  }

  // 1. 累加总面积，并求面积加权的质心 y
  double total_area = 0.0;
  double weighted_y = 0.0;
  for (const LeakItem& item : items) {
    const double area = static_cast<double>(item.area);
    total_area += area;
    weighted_y += area * MaskCentroidY(item.mask);
  }
  const double centroid_y = total_area > 0.0 ? weighted_y / total_area : 0.0;

  // 2. 三个历史队列入队
  area_history_.push_back(total_area);
  centroid_history_.push_back(centroid_y);
  hit_history_.push_back(items.empty() ? 0 : 1);

  // 3. 超出窗口长度则弹出最早的一帧
  while (static_cast<int>(area_history_.size()) > config_.window) {
    area_history_.pop_front();
  }
  while (static_cast<int>(centroid_history_.size()) > config_.window) {
    centroid_history_.pop_front();
  }
  while (static_cast<int>(hit_history_.size()) > config_.window) {
    hit_history_.pop_front();
  }

  // 4. 窗口未满：不做趋势判定，但仍如实返回预热期的命中数，
  //    这样操作员能看到"已经在检出目标，只是还没到判定窗口"。
  if (static_cast<int>(area_history_.size()) < config_.window) {
    state.alert = false;
    state.hit_count = std::accumulate(hit_history_.begin(), hit_history_.end(), 0);
    state.area_growing = false;
    state.centroid_down = false;
    return state;
  }

  // 5. 窗口已满，做趋势判定
  const int hits = std::accumulate(hit_history_.begin(), hit_history_.end(), 0);

  const bool area_growing = area_history_.back() >
                            area_history_.front() * static_cast<double>(config_.area_growth_ratio);
  const bool centroid_down = centroid_history_.back() >
                             centroid_history_.front() +
                                 static_cast<double>(config_.centroid_down_px);

  // 6. 填充状态
  state.hit_count = hits;
  state.area_growing = area_growing;
  state.centroid_down = centroid_down;
  state.alert = (hits >= config_.min_hits) && (area_growing || centroid_down);
  return state;
}

void LeakFilter::Reset() {
  area_history_.clear();
  centroid_history_.clear();
  hit_history_.clear();
}

}  // namespace leak
