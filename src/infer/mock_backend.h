#pragma once
#include "infer/infer_backend.h"

namespace cvsdk {
class MockBackend final : public InferBackend {
 public:
  CVSDK_Status Load(const std::string& package_dir) override;
  CVSDK_Status Run(const CVSDK_Image& image, std::vector<Detection>* output) override;
  BackendCapabilities Capabilities() const override { return {}; }
};
}
