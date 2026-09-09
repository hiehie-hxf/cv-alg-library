#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {
/** 设置当前线程最近一次错误；C ABI 通过 CVSDK_GetLastError 暴露。 */
void SetLastError(std::string message);
const char* LastError();
const char* StatusMessage(CVSDK_Status status);
} // namespace cvsdk
