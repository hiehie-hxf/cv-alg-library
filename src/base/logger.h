#pragma once

#include "cv_sdk/cv_sdk.h"
#include <string>

namespace cvsdk {
/** SDK 全局异步日志门面；实现使用有界队列，避免阻塞实时推理线程。 */
class Logger {
public:
  static Logger& Instance();
  /** 输入：日志配置；输出：配置状态码。 */
  CVSDK_Status Configure(const CVSDK_LogOptions* options);
  /** 输入：等级、模块和消息；输出：异步入队，无返回值。 */
  void Log(CVSDK_LogLevel level, const char* module, const char* message);
  /** 输出：填充日志统计并返回状态码。 */
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
