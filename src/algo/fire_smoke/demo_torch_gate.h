#pragma once
#include "algo/fire_smoke/fire_config.h"
#include "cv_sdk/cv_sdk.h"
#include <vector>
namespace cvsdk {
std::vector<CVSDK_Detection> DetectDemoTorch(const CVSDK_Image& image, const FireConfig& config);
}
