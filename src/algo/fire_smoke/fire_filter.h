#pragma once

#include "algo/fire_smoke/fire_config.h"
#include <deque>

namespace cvsdk {
/**
 * 火焰/烟雾滑动窗口告警器。
 * 该类保存跨帧状态，因此禁止多个摄像头共享同一实例，也不保证并发调用安全。
 */
class FireFilter {
public:
  explicit FireFilter(FireConfig config) : config_(config) {}
  /** 输入：图像尺寸和检测框；输出：更新告警状态及状态码。 */
  CVSDK_Status Process(uint32_t width, uint32_t height, const CVSDK_Detection* detections,
                       uint32_t count, CVSDK_FireAlertState* state);
  /** 输入：无；输出：清空 fire/smoke 滑动窗口。 */
  void Reset();

private:
  FireConfig config_;
  std::deque<float> fire_, smoke_;
};
} // namespace cvsdk
