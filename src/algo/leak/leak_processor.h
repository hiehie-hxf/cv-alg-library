#pragma once

#include "algo/leak/leak_config.h"
#include "algo/leak/leak_filter.h"
#include "algo/leak/leak_segmenter.h"
#include <vector>

namespace cvsdk {
/** 漏液后处理编排器，固定执行单帧分割和跨帧时序告警。 */
class LeakProcessor {
public:
  explicit LeakProcessor(LeakConfig config) : config_(config), filter_(config) {}
  /** 输入：模型包目录和 backend 名称；输出：加载分割器会话并返回状态码。 */
  CVSDK_Status Init(const char* package_dir, const char* backend);
  /** 输入：一帧图像；输出：漏液目标、告警状态及状态码。 */
  CVSDK_Status Process(const CVSDK_Image& image, std::vector<LeakItem>* items,
                       CVSDK_LeakAlertState* state);
  /**
   * 输入：目标下标、调用方缓冲及容量；输出：掩码尺寸、行跨度和状态码。
   * buffer 传 NULL 时只查询尺寸；掩码只在当前帧有效，下一次 Process 或 Reset 后失效。
   */
  CVSDK_Status CopyMask(uint32_t index, uint8_t* buffer, uint32_t capacity, uint32_t* out_width,
                        uint32_t* out_height, uint32_t* out_stride) const;
  /** 输入：无；输出：清空跨帧时序状态和缓存的上一帧结果，保留已加载的模型会话。 */
  void Reset() {
    filter_.Reset();
    last_items_.clear();
  }

private:
  LeakConfig config_;
  LeakSegmenter segmenter_;
  LeakFilter filter_;
  std::vector<LeakItem> last_items_; // 上一帧结果，供 CopyMask 取走实例掩码
};
} // namespace cvsdk
