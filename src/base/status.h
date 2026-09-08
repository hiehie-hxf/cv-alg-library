#pragma once

#include <string>
#include "cv_sdk/cv_sdk.h"

namespace cvsdk {
void SetLastError(std::string message);
const char* LastError();
const char* StatusMessage(CVSDK_Status status);
}
