#include "algo/detector.h"
#include <cstring>
#include "base/status.h"
#include "infer/mock_backend.h"

namespace cvsdk {
CVSDK_Status Detector::Init(const char* package_dir, const CVSDK_DetectorOptions* options) {
  const char* backend = options && options->backend ? options->backend : "mock";
  if (std::strcmp(backend, "mock") != 0) {
    SetLastError("requested backend is not built; MVP provides mock only");
    return CVSDK_UNSUPPORTED;
  }
  if (options && options->score_threshold >= 0.F && options->score_threshold <= 1.F)
    score_threshold_ = options->score_threshold;
  backend_ = std::make_unique<MockBackend>();
  return backend_->Load(package_dir);
}
CVSDK_Status Detector::Infer(const CVSDK_Image& image, std::vector<Detection>* result) {
  std::vector<Detection> raw;
  CVSDK_Status status = backend_->Run(image, &raw);
  if (status != CVSDK_OK) return status;
  result->clear();
  for (const auto& item : raw) if (item.score >= score_threshold_) result->push_back(item);
  return CVSDK_OK;
}
}
