#pragma once

#include "algo/fire_smoke/fire_config.h"
#include <deque>

namespace cvsdk {
class FireFilter {
public:
  explicit FireFilter(FireConfig config) : config_(config) {}
  CVSDK_Status Process(uint32_t width, uint32_t height, const CVSDK_Detection* detections,
                       uint32_t count, CVSDK_FireAlertState* state);
  void Reset();

private:
  FireConfig config_;
  std::deque<float> fire_, smoke_;
};
} // namespace cvsdk
