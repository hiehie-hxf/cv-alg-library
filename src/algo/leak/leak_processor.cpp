#include "algo/leak/leak_processor.h"

#include "base/status.h"
#include <cstddef>
#include <cstring>

namespace cvsdk {
// 编排顺序：单帧分割 -> 多帧告警；处理器不改变分割结果本身，只做状态推进。
CVSDK_Status LeakProcessor::Init(const char* package_dir, const char* backend) {
  return segmenter_.Init(package_dir, config_, backend);
}

CVSDK_Status LeakProcessor::Process(const CVSDK_Image& image, std::vector<LeakItem>* items,
                                    CVSDK_LeakAlertState* state) {
  if (!items || !state || !image.data || image.width == 0 || image.height == 0) {
    SetLastError("invalid leak processor input");
    return CVSDK_INVALID_ARGUMENT;
  }
  CVSDK_Status status = segmenter_.Run(image, items);
  if (status != CVSDK_OK)
    return status;
  // 缓存本帧结果：掩码不放进 C ABI 的结构体，改由 CopyMask 单独取走。
  last_items_ = *items;
  return filter_.Process(*items, state);
}

CVSDK_Status LeakProcessor::CopyMask(uint32_t index, uint8_t* buffer, uint32_t capacity,
                                     uint32_t* out_width, uint32_t* out_height,
                                     uint32_t* out_stride) const {
  if (!out_width || !out_height || !out_stride) {
    SetLastError("invalid leak mask output arguments");
    return CVSDK_INVALID_ARGUMENT;
  }
  if (index >= last_items_.size()) {
    SetLastError("leak mask index is out of range");
    return CVSDK_INVALID_ARGUMENT;
  }
  const LeakItem& item = last_items_[index];
  const uint32_t width = static_cast<uint32_t>(item.mask_width);
  const uint32_t height = static_cast<uint32_t>(item.mask_height);
  *out_width = width;
  *out_height = height;
  *out_stride = width;
  const std::size_t required = static_cast<std::size_t>(width) * height;
  if (width == 0 || height == 0 || item.mask.size() < required) {
    SetLastError("leak mask is not available");
    return CVSDK_NOT_FOUND;
  }
  // 首次传 buffer=NULL 只查询尺寸；与 SDK 其他输出数组的容量查询协议保持一致，
  // 这条路径属于正常用法，因此不写入错误信息。
  if (!buffer)
    return CVSDK_BUFFER_TOO_SMALL;
  if (capacity < required) {
    SetLastError("leak mask buffer capacity is insufficient");
    return CVSDK_BUFFER_TOO_SMALL;
  }
  std::memcpy(buffer, item.mask.data(), required);
  return CVSDK_OK;
}
} // namespace cvsdk
