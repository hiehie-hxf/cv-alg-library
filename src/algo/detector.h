#pragma once
#include "infer/infer_backend.h"
#include <memory>
#include <vector>

namespace cvsdk {
/** 通用检测任务适配器：负责后端选择、模型加载和统一置信度过滤。 */
class Detector {
public:
  CVSDK_Status Init(const char* package_dir, const CVSDK_DetectorOptions* options);
  CVSDK_Status Infer(const CVSDK_Image& image, std::vector<Detection>* result);

private:
  float score_threshold_ = .25F;
  std::unique_ptr<InferBackend> backend_;
};
} // namespace cvsdk
