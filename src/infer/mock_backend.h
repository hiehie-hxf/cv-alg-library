#pragma once
#include "infer/infer_backend.h"

namespace cvsdk {
/** 用于 API 和规则测试的确定性后端，不代表生产模型推理结果。 */
class MockBackend final : public InferBackend {
public:
  CVSDK_Status Load(const std::string& package_dir) override;
  CVSDK_Status Run(const CVSDK_Image& image, std::vector<Detection>* output) override;
  BackendCapabilities Capabilities() const override {
    return {};
  }
};
} // namespace cvsdk
