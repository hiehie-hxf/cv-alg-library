#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>
#include <vector>

namespace cvsdk {
/** 后端无关的检测框，禁止在业务层暴露 TensorRT/ONNX Runtime 类型。 */
struct Detection {
  float x, y, width, height, score;
  int32_t class_id;
};
/** 后端能力声明，供运行时决定是否启用异步或动态 batch。 */
struct BackendCapabilities {
  bool supports_async = false;
  bool supports_dynamic_batch = false;
};
class InferBackend {
public:
  virtual ~InferBackend() = default;
  /** 输入：模型包目录；输出：加载状态码。 */
  virtual CVSDK_Status Load(const std::string& package_dir) = 0;
  /** 输入：图像；输出：检测框数组和状态码。 */
  virtual CVSDK_Status Run(const CVSDK_Image& image, std::vector<Detection>* output) = 0;
  virtual BackendCapabilities Capabilities() const = 0;
};
} // namespace cvsdk
