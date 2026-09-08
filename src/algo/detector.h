#pragma once
#include <memory>
#include <vector>
#include "infer/infer_backend.h"

namespace cvsdk {
class Detector {
 public:
  CVSDK_Status Init(const char* package_dir, const CVSDK_DetectorOptions* options);
  CVSDK_Status Infer(const CVSDK_Image& image, std::vector<Detection>* result);
 private:
  float score_threshold_ = .25F;
  std::unique_ptr<InferBackend> backend_;
};
}
