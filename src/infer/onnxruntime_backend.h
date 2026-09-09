#pragma once
#include "infer/infer_backend.h"
#include <memory>
namespace cvsdk {
class OnnxRuntimeBackend final : public InferBackend {
public:
  OnnxRuntimeBackend();
  ~OnnxRuntimeBackend() override;
  CVSDK_Status Load(const std::string& package_dir) override;
  CVSDK_Status Run(const CVSDK_Image& image, std::vector<Detection>* output) override;
  BackendCapabilities Capabilities() const override {
    return {};
  }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cvsdk
