#pragma once
#include "algo/fire_smoke/fire_config.h"
#include "cv_sdk/cv_sdk.h"
#include <vector>
namespace cvsdk {
/** 可选的演示火把规则；默认关闭，不用于真实火焰模型判断。 */
std::vector<CVSDK_Detection> DetectDemoTorch(const CVSDK_Image& image, const FireConfig& config);
} // namespace cvsdk
