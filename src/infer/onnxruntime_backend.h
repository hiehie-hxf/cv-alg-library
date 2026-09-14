#pragma once
#include "infer/infer_backend.h"
#include <memory>
namespace cvsdk {
/** ONNX Runtime CPU 推理后端；平台加速 provider 在后续硬件适配中配置。 */
class OnnxRuntimeBackend final : public InferBackend {
public:
  enum class Provider { Cpu, Cuda };
  explicit OnnxRuntimeBackend(Provider provider = Provider::Cpu);
  /** 输出：释放 ONNX Runtime 会话和内部资源。 */
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
