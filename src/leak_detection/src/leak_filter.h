#pragma once

#include <deque>
#include <vector>

#include "leak_detector.h"

namespace leak {

// 时序判定：累积多帧的漏液总面积与质心，出现持续增长/下移趋势后给出告警。
class LeakFilter {
 public:
  LeakFilter();
  explicit LeakFilter(const LeakConfig& config);
  ~LeakFilter();

  LeakFilter(const LeakFilter&) = delete;
  LeakFilter& operator=(const LeakFilter&) = delete;

  void Configure(const LeakConfig& config);

  // 处理一帧检测结果，返回当前告警状态。
  LeakAlertState Process(const std::vector<LeakItem>& items);

  void Reset();

 private:
  LeakConfig config_;
  std::deque<double> area_history_;      // 每帧漏液总掩码面积
  std::deque<double> centroid_history_;  // 每帧面积加权质心 y
  std::deque<int> hit_history_;          // 每帧是否命中
};

}  // namespace leak
