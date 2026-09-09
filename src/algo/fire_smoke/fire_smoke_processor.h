#pragma once

#include "algo/fire_smoke/fire_color_gate.h"
#include "algo/fire_smoke/fire_filter.h"
#include "algo/fire_smoke/smoke_static_gate.h"
#include <vector>

namespace cvsdk {
/** 火情后处理编排器，固定执行颜色门控、演示规则、烟雾门控和时序告警。 */
class FireSmokeProcessor {
public:
  explicit FireSmokeProcessor(FireConfig config)
      : config_(config), filter_(config), smoke_gate_(config) {}
  /** 输入：原始图像和检测框；输出：过滤框、告警状态及状态码。 */
  CVSDK_Status Process(const CVSDK_Image& image, const CVSDK_Detection* input, uint32_t count,
                       std::vector<CVSDK_Detection>* filtered, CVSDK_FireAlertState* state);
  /** 输入：无；输出：清空所有跨帧门控状态。 */
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
