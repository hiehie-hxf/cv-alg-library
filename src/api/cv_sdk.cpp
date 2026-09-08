#include "cv_sdk/cv_sdk.h"
#include <exception>
#include <memory>
#include <vector>
#include "algo/detector.h"
#include "base/status.h"
#include "base/logger.h"

struct CVSDK_Detector { cvsdk::Detector impl; };
namespace {
bool HasSize(uint32_t actual, size_t required) { return actual >= required; }
CVSDK_Status ValidateImage(const CVSDK_Image* image) {
  if (!image || !HasSize(image->struct_size, sizeof(CVSDK_Image)) || !image->data ||
      image->width == 0 || image->height == 0 || image->stride_bytes < image->width ||
      (image->pixel_format != CVSDK_PIXEL_FORMAT_BGR8 && image->pixel_format != CVSDK_PIXEL_FORMAT_RGB8 && image->pixel_format != CVSDK_PIXEL_FORMAT_GRAY8)) {
    cvsdk::SetLastError("invalid CVSDK_Image"); return CVSDK_INVALID_ARGUMENT;
  }
  uint32_t channels = image->pixel_format == CVSDK_PIXEL_FORMAT_GRAY8 ? 1 : 3;
  if (image->stride_bytes < image->width * channels) { cvsdk::SetLastError("image stride is too small"); return CVSDK_INVALID_ARGUMENT; }
  return CVSDK_OK;
}
}
extern "C" {
uint32_t CVSDK_GetApiVersion(void) { return CVSDK_API_VERSION; }
const char* CVSDK_StatusMessage(CVSDK_Status status) { return cvsdk::StatusMessage(status); }
const char* CVSDK_GetLastError(void) { return cvsdk::LastError(); }
CVSDK_Status CVSDK_ConfigureLogging(const CVSDK_LogOptions* options) { return cvsdk::Logger::Instance().Configure(options); }
CVSDK_Status CVSDK_GetLogStats(CVSDK_LogStats* stats) { return cvsdk::Logger::Instance().GetStats(stats); }
void CVSDK_Log(CVSDK_LogLevel level, const char* module, const char* message) { cvsdk::Logger::Instance().Log(level, module, message); }
CVSDK_Status CVSDK_DetectorCreate(const char* package_dir, const CVSDK_DetectorOptions* options, CVSDK_Detector** out_detector) {
  try {
    if (!package_dir || !out_detector || (options && !HasSize(options->struct_size, sizeof(CVSDK_DetectorOptions)))) { cvsdk::SetLastError("invalid detector create arguments"); return CVSDK_INVALID_ARGUMENT; }
    *out_detector = nullptr;
    auto detector = std::make_unique<CVSDK_Detector>();
    CVSDK_Status status = detector->impl.Init(package_dir, options);
    if (status != CVSDK_OK) return status;
    *out_detector = detector.release(); CVSDK_Log(CVSDK_LOG_INFO, "algo.detector", "detector created"); return CVSDK_OK;
  } catch (const std::bad_alloc&) { cvsdk::SetLastError("allocation failed"); return CVSDK_OUT_OF_MEMORY;
  } catch (const std::exception& error) { cvsdk::SetLastError(error.what()); return CVSDK_INTERNAL_ERROR;
  } catch (...) { cvsdk::SetLastError("unknown internal exception"); return CVSDK_INTERNAL_ERROR; }
}
CVSDK_Status CVSDK_DetectorInfer(CVSDK_Detector* detector, const CVSDK_Image* image, CVSDK_DetectionList* list) {
  try {
    if (!detector || !list || !HasSize(list->struct_size, sizeof(CVSDK_DetectionList))) { cvsdk::SetLastError("invalid detector infer arguments"); return CVSDK_INVALID_ARGUMENT; }
    CVSDK_Status status = ValidateImage(image); if (status != CVSDK_OK) { CVSDK_Log(CVSDK_LOG_WARN, "api.detector", "invalid inference image"); return status; }
    std::vector<cvsdk::Detection> result; status = detector->impl.Infer(*image, &result); if (status != CVSDK_OK) return status;
    list->count = static_cast<uint32_t>(result.size());
    if (!list->items) return result.empty() ? CVSDK_OK : CVSDK_BUFFER_TOO_SMALL;
    if (list->capacity < list->count) { cvsdk::SetLastError("detection buffer capacity is insufficient"); return CVSDK_BUFFER_TOO_SMALL; }
    for (uint32_t i = 0; i < list->count; ++i) list->items[i] = {result[i].x, result[i].y, result[i].width, result[i].height, result[i].score, result[i].class_id};
    return CVSDK_OK;
  } catch (const std::exception& error) { cvsdk::SetLastError(error.what()); return CVSDK_INTERNAL_ERROR;
  } catch (...) { cvsdk::SetLastError("unknown internal exception"); return CVSDK_INTERNAL_ERROR; }
}
void CVSDK_DetectorDestroy(CVSDK_Detector* detector) { delete detector; }
}
