#pragma once

#include "cv_sdk/cv_sdk.h"
#include <memory>
#include <string>
#include <vector>

namespace cvsdk {
class GaugeReader {
public:
  GaugeReader();
  ~GaugeReader();
  CVSDK_Status Init(const char* package_dir, const CVSDK_GaugeReaderOptions* options);
  CVSDK_Status Infer(const CVSDK_Image& image, std::vector<CVSDK_GaugeReading>* output);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cvsdk
