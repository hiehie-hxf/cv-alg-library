#pragma once

#include "algo/digital_gauge/digital_gauge_config.h"
#include "cv_sdk/cv_sdk.h"
#include <vector>

namespace cvsdk {

/** 纯 OpenCV 数字仪表读取器；实例初始化后可重复处理多个同步图像。 */
class DigitalGaugeReader {
public:
  /** 加载设备标定包和运行参数。 */
  CVSDK_Status Init(const char* package_dir, const CVSDK_DigitalGaugeReaderOptions* options);
  /** 定位图中所有面板并输出每个面板的 PV/SV 行。 */
  CVSDK_Status Infer(const CVSDK_Image& image,
                     std::vector<CVSDK_DigitalGaugeReading>* output) const;

private:
  DigitalGaugeConfig config_;
  float minimum_confidence_ = 0.0F;
  bool return_ambiguous_ = true;
  bool initialized_ = false;
};

} // namespace cvsdk
