#pragma once

#include "cv_sdk/cv_sdk.h"

namespace cvsdk {
/** 输入：BGR/RGB 图像和原图坐标框；输出：框内高亮暖色像素比例 [0,1]。 */
float FireColorFraction(const CVSDK_Image& image, const CVSDK_Detection& box);
/** 输入：图像、候选框和最小比例；输出：是否通过明火颜色门控。 */
bool PassFireColorGate(const CVSDK_Image& image, const CVSDK_Detection& box, float min_fraction);
} // namespace cvsdk
