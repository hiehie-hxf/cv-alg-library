#include "base/status.h"

namespace cvsdk {
thread_local std::string g_last_error;
void SetLastError(std::string message) {
  g_last_error = std::move(message);
}
const char* LastError() {
  return g_last_error.c_str();
}
const char* StatusMessage(CVSDK_Status status) {
  switch (status) {
  case CVSDK_OK:
    return "success";
  case CVSDK_INVALID_ARGUMENT:
    return "invalid argument";
  case CVSDK_NOT_FOUND:
    return "not found";
  case CVSDK_UNSUPPORTED:
    return "unsupported";
  case CVSDK_OUT_OF_MEMORY:
    return "out of memory";
  case CVSDK_INTERNAL_ERROR:
    return "internal error";
  case CVSDK_BUFFER_TOO_SMALL:
    return "output buffer too small";
  }
  return "unknown status";
}
} // namespace cvsdk
