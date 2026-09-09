#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {
void SetLastError(std::string message);
const char* LastError();
const char* StatusMessage(CVSDK_Status status);
} // namespace cvsdk
