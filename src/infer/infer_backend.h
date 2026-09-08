#pragma once

#include <string>
#include <vector>
#include "cv_sdk/cv_sdk.h"

namespace cvsdk {
struct Detection { float x, y, width, height, score; int32_t class_id; };
struct BackendCapabilities { bool supports_async = false; bool supports_dynamic_batch = false; };
class InferBackend {
 public:
  virtual ~InferBackend() = default;
  virtual CVSDK_Status Load(const std::string& package_dir) = 0;
  virtual CVSDK_Status Run(const CVSDK_Image& image, std::vector<Detection>* output) = 0;
  virtual BackendCapabilities Capabilities() const = 0;
};
}
