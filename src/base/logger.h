#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {
class Logger {
public:
  static Logger& Instance();
  CVSDK_Status Configure(const CVSDK_LogOptions* options);
  void Log(CVSDK_LogLevel level, const char* module, const char* message);
  CVSDK_Status GetStats(CVSDK_LogStats* out_stats);
  ~Logger();

private:
  Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  struct Impl;
  Impl* impl_;
};
} // namespace cvsdk
