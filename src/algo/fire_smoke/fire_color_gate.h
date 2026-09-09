#pragma once

#include "cv_sdk/cv_sdk.h"

namespace cvsdk {
/* Returns the fraction of high-brightness, saturated warm pixels inside the box. */
float FireColorFraction(const CVSDK_Image& image, const CVSDK_Detection& box);
bool PassFireColorGate(const CVSDK_Image& image, const CVSDK_Detection& box, float min_fraction);
} // namespace cvsdk
