#pragma once
#include "infer/infer_backend.h"
#include <memory>
#include <vector>

namespace cvsdk {
class Detector {
public:
  CVSDK_Status Init(const char* package_dir, const CVSDK_DetectorOptions* options);
  CVSDK_Status Infer(const CVSDK_Image& image, std::vector<Detection>* result);

private:
  float score_threshold_ = .25F;
  std::unique_ptr<InferBackend> backend_;
};
} // namespace cvsdk
