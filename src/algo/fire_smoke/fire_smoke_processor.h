#pragma once

#include "algo/fire_smoke/fire_color_gate.h"
#include "algo/fire_smoke/fire_filter.h"
#include "algo/fire_smoke/smoke_static_gate.h"
#include <vector>

namespace cvsdk {
class FireSmokeProcessor {
public:
  explicit FireSmokeProcessor(FireConfig config)
      : config_(config), filter_(config), smoke_gate_(config) {}
  CVSDK_Status Process(const CVSDK_Image& image, const CVSDK_Detection* input, uint32_t count,
                       std::vector<CVSDK_Detection>* filtered, CVSDK_FireAlertState* state);
  void Reset() {
    filter_.Reset();
    smoke_gate_.Reset();
  }

private:
  FireConfig config_;
  FireFilter filter_;
  SmokeStaticGate smoke_gate_;
};
} // namespace cvsdk
