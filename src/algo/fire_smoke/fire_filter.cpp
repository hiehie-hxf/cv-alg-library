#include "algo/fire_smoke/fire_filter.h"

#include "base/status.h"
#include <algorithm>
#include <cstdio>

// 告警器只消费已完成图像门控的检测框；模型推理和业务规则因此可以独立回归测试。

namespace cvsdk {
namespace {
void Push(std::deque<float>* values, uint32_t window, float value) {
  values->push_back(value);
  while (values->size() > window)
    values->pop_front();
}
float Max(const std::deque<float>& values) {
  return values.empty() ? 0.F : *std::max_element(values.begin(), values.end());
}
uint32_t Hits(const std::deque<float>& values) {
  return static_cast<uint32_t>(
      std::count_if(values.begin(), values.end(), [](float x) { return x > 0.F; }));
}
} // namespace
void FireFilter::Reset() {
  fire_.clear();
  smoke_.clear();
}
CVSDK_Status FireFilter::Process(uint32_t width, uint32_t height, const CVSDK_Detection* detections,
                                 uint32_t count, CVSDK_FireAlertState* state) {
  if (!state || state->struct_size < sizeof(CVSDK_FireAlertState) || width == 0 || height == 0 ||
      (count && !detections)) {
    SetLastError("invalid fire filter input");
    return CVSDK_INVALID_ARGUMENT;
  }
  // 每帧仅保留同类最高置信度，避免同一目标多个框重复计数。
  float frame_fire = 0.F, frame_smoke = 0.F;
  const float image_area = static_cast<float>(width) * height;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& d = detections[i];
    if (d.score < 0 || d.score > 1 || d.width < 0 || d.height < 0)
      continue;
    const float area_ratio = d.width * d.height / image_area;
    if (area_ratio < config_.min_area_ratio)
      continue;
    if (d.class_id == 1)
      frame_fire = std::max(frame_fire, d.score);
    else if (d.class_id == 0)
      frame_smoke = std::max(frame_smoke, d.score);
  }
  Push(&fire_, config_.fire_window, frame_fire >= config_.fire_candidate_conf ? frame_fire : 0.F);
  Push(&smoke_, config_.smoke_window,
       frame_smoke >= config_.smoke_candidate_conf ? frame_smoke : 0.F);
  const uint32_t fire_hits = Hits(fire_), smoke_hits = Hits(smoke_);
  const float fire_max = Max(fire_), smoke_max = Max(smoke_);
  const bool fire_confirmed =
      fire_hits >= config_.fire_min_hits && fire_max >= config_.fire_confirm_conf;
  const bool smoke_confirmed =
      smoke_hits >= config_.smoke_min_hits && smoke_max >= config_.smoke_confirm_conf;
  const bool critical =
      fire_hits >= config_.fire_min_hits && fire_max >= config_.critical_fire_conf;
  state->level = CVSDK_FIRE_ALERT_NONE;
  const char* reason = "no_detection";
  if (critical || (fire_confirmed && smoke_confirmed)) {
    state->level = CVSDK_FIRE_ALERT_CRITICAL;
    reason = critical ? "fire_high_confidence_sustained" : "fire_and_smoke_confirmed";
  } else if (fire_confirmed) {
    state->level = CVSDK_FIRE_ALERT_WARNING_FIRE;
    reason = "fire_confirmed";
  } else if (smoke_confirmed) {
    state->level = CVSDK_FIRE_ALERT_WARNING_SMOKE;
    reason = "smoke_confirmed";
  } else if (fire_hits || smoke_hits) {
    state->level = CVSDK_FIRE_ALERT_INFO;
    reason = "candidate_detected";
  }
  state->max_fire_confidence = fire_max;
  state->max_smoke_confidence = smoke_max;
  state->fire_hits = fire_hits;
  state->smoke_hits = smoke_hits;
  std::snprintf(state->reason, sizeof(state->reason), "%s", reason);
  return CVSDK_OK;
}
} // namespace cvsdk
