#pragma once

#include "algo/leak/leak_config.h"
#include "algo/leak/leak_segmenter.h"
#include <deque>
#include <vector>

namespace cvsdk {
/**
 * 漏液滑动窗口告警器。
 * 该类保存跨帧状态，因此禁止多个摄像头共享同一实例，也不保证并发调用安全。
 */
class LeakFilter {
public:
  explicit LeakFilter(LeakConfig config) : config_(config) {}
  /** 输入：当前帧漏液目标列表；输出：更新告警状态及状态码。 */
  CVSDK_Status Process(const std::vector<LeakItem>& items, CVSDK_LeakAlertState* state);
  /** 输入：无；输出：清空面积、质心和命中滑动窗口。 */
  void Reset();

private:
  LeakConfig config_;
  std::deque<double> area_history_, centroid_history_;
  std::deque<uint32_t> hit_history_;
};
} // namespace cvsdk
