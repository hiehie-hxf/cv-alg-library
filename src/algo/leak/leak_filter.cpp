#include "algo/leak/leak_filter.h"

#include "base/status.h"
#include <algorithm>
#include <cstdio>

// 告警器只消费已完成掩码还原的漏液目标；模型推理和业务规则因此可以独立回归测试。

namespace cvsdk {
namespace {
/** 输入：当前帧目标列表；输出：掩码总面积。 */
double TotalArea(const std::vector<LeakItem>& items) {
  double total = 0.0;
  for (const auto& item : items)
    total += static_cast<double>(item.area);
  return total;
}

/** 输入：当前帧目标列表和总面积；输出：按面积加权的质心 y，无目标时为 0。 */
double WeightedCentroidY(const std::vector<LeakItem>& items, double total_area) {
  if (total_area <= 0.0)
    return 0.0;
  double weighted = 0.0;
  for (const auto& item : items)
    weighted += static_cast<double>(item.area) * item.centroid_y;
  return weighted / total_area;
}
} // namespace

CVSDK_Status LeakFilter::Process(const std::vector<LeakItem>& items, CVSDK_LeakAlertState* state) {
  if (!state || state->struct_size < sizeof(CVSDK_LeakAlertState) || config_.window == 0) {
    SetLastError("invalid leak filter input");
    return CVSDK_INVALID_ARGUMENT;
  }
  const double total_area = TotalArea(items);
  const double centroid_y = WeightedCentroidY(items, total_area);

  area_history_.push_back(total_area);
  centroid_history_.push_back(centroid_y);
  hit_history_.push_back(items.empty() ? 0U : 1U);
  while (area_history_.size() > config_.window)
    area_history_.pop_front();
  while (centroid_history_.size() > config_.window)
    centroid_history_.pop_front();
  while (hit_history_.size() > config_.window)
    hit_history_.pop_front();

  const uint32_t hits = static_cast<uint32_t>(std::count_if(
      hit_history_.begin(), hit_history_.end(), [](uint32_t value) { return value != 0; }));

  state->level = CVSDK_LEAK_ALERT_NONE;
  state->leak_count = static_cast<uint32_t>(items.size());
  state->hits = hits;
  state->total_area = static_cast<uint32_t>(total_area);
  state->centroid_y = static_cast<float>(centroid_y);
  state->max_confidence = 0.F;
  for (const auto& item : items)
    state->max_confidence = std::max(state->max_confidence, item.score);
  const char* reason = items.empty() ? "no_detection" : "candidate_detected";

  // 窗口未满时只累计命中数，不做趋势判定，避免冷启动阶段误报。
  if (area_history_.size() < config_.window) {
    state->window_filled = 0;
    state->area_growing = 0;
    state->centroid_down = 0;
    std::snprintf(state->reason, sizeof(state->reason), "%s", reason);
    return CVSDK_OK;
  }

  const bool area_growing =
      area_history_.back() > area_history_.front() * config_.area_growth_ratio;
  const bool centroid_down =
      centroid_history_.back() > centroid_history_.front() + config_.centroid_down_px;
  state->window_filled = 1;
  state->area_growing = area_growing ? 1U : 0U;
  state->centroid_down = centroid_down ? 1U : 0U;
  // 命中数达标且出现扩散趋势才告警，静止液渍不会反复误报。
  if (hits >= config_.min_hits && (area_growing || centroid_down)) {
    state->level = CVSDK_LEAK_ALERT_WARNING;
    reason = area_growing ? "area_growing" : "centroid_down";
  } else if (hits >= config_.min_hits) {
    reason = "hits_confirmed_no_trend";
  } else {
    reason = "insufficient_hits";
  }
  std::snprintf(state->reason, sizeof(state->reason), "%s", reason);
  return CVSDK_OK;
}

void LeakFilter::Reset() {
  area_history_.clear();
  centroid_history_.clear();
  hit_history_.clear();
}
} // namespace cvsdk
