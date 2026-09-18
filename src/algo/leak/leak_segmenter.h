#pragma once

#include "algo/leak/leak_config.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace cvsdk {
/** 单个漏液目标；掩码为原图尺寸、行优先的 0/255 缓冲，仅在本模块内部使用。 */
struct LeakItem {
  float x = 0.F, y = 0.F, width = 0.F, height = 0.F, score = 0.F;
  float centroid_x = 0.F, centroid_y = 0.F;
  uint32_t area = 0;
  int mask_width = 0, mask_height = 0;
  std::vector<uint8_t> mask;
};

/**
 * YOLOv8-Seg 单帧分割器。
 * 持有 ONNX Runtime 会话，负责 letterbox 前处理、推理和实例掩码还原；
 * 保存的是模型句柄而非业务状态，同一实例可连续处理同一路视频的帧，
 * 但禁止多线程并发调用。推理后端专有类型不向任务层暴露。
 */
class LeakSegmenter {
public:
  LeakSegmenter();
  ~LeakSegmenter();
  /** 输入：模型包目录、配置和 backend 名称；输出：加载会话并返回状态码。 */
  CVSDK_Status Init(const char* package_dir, const LeakConfig& config, const char* backend);
  /** 输入：一帧 BGR/RGB 图像；输出：该帧的漏液目标列表和状态码。 */
  CVSDK_Status Run(const CVSDK_Image& image, std::vector<LeakItem>* output);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cvsdk
