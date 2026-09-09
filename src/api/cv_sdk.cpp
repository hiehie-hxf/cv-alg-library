#include "cv_sdk/cv_sdk.h"
#include "algo/detector.h"
#include "algo/fire_smoke/fire_config.h"
#include "algo/fire_smoke/fire_filter.h"
#include "algo/fire_smoke/fire_smoke_processor.h"
#include "base/logger.h"
#include "base/status.h"
#include <exception>
#include <memory>
#include <vector>

// C ABI 是唯一公开边界；所有 C++ 异常在此转换为稳定错误码。

struct CVSDK_Detector {
  cvsdk::Detector impl;
};
struct CVSDK_FireFilter {
  cvsdk::FireFilter impl;
  explicit CVSDK_FireFilter(cvsdk::FireConfig config) : impl(config) {}
};
struct CVSDK_FireSmokeProcessor {
  cvsdk::FireSmokeProcessor impl;
  explicit CVSDK_FireSmokeProcessor(cvsdk::FireConfig config) : impl(config) {}
};
namespace {
bool HasSize(uint32_t actual, size_t required) {
  return actual >= required;
}
CVSDK_Status ValidateImage(const CVSDK_Image* image) {
  if (!image || !HasSize(image->struct_size, sizeof(CVSDK_Image)) || !image->data ||
      image->width == 0 || image->height == 0 || image->stride_bytes < image->width ||
      (image->pixel_format != CVSDK_PIXEL_FORMAT_BGR8 &&
       image->pixel_format != CVSDK_PIXEL_FORMAT_RGB8 &&
       image->pixel_format != CVSDK_PIXEL_FORMAT_GRAY8)) {
    cvsdk::SetLastError("invalid CVSDK_Image");
    return CVSDK_INVALID_ARGUMENT;
  }
  uint32_t channels = image->pixel_format == CVSDK_PIXEL_FORMAT_GRAY8 ? 1 : 3;
  if (image->stride_bytes < image->width * channels) {
    cvsdk::SetLastError("image stride is too small");
    return CVSDK_INVALID_ARGUMENT;
  }
  return CVSDK_OK;
}
} // namespace
extern "C" {
uint32_t CVSDK_GetApiVersion(void) {
  return CVSDK_API_VERSION;
}
const char* CVSDK_StatusMessage(CVSDK_Status status) {
  return cvsdk::StatusMessage(status);
}
const char* CVSDK_GetLastError(void) {
  return cvsdk::LastError();
}
CVSDK_Status CVSDK_ConfigureLogging(const CVSDK_LogOptions* options) {
  return cvsdk::Logger::Instance().Configure(options);
}
CVSDK_Status CVSDK_GetLogStats(CVSDK_LogStats* stats) {
  return cvsdk::Logger::Instance().GetStats(stats);
}
void CVSDK_Log(CVSDK_LogLevel level, const char* module, const char* message) {
  cvsdk::Logger::Instance().Log(level, module, message);
}
CVSDK_Status CVSDK_DetectorCreate(const char* package_dir, const CVSDK_DetectorOptions* options,
                                  CVSDK_Detector** out_detector) {
  try {
    if (!package_dir || !out_detector ||
        (options && !HasSize(options->struct_size, sizeof(CVSDK_DetectorOptions)))) {
      cvsdk::SetLastError("invalid detector create arguments");
      return CVSDK_INVALID_ARGUMENT;
    }
    *out_detector = nullptr;
    auto detector = std::make_unique<CVSDK_Detector>();
    CVSDK_Status status = detector->impl.Init(package_dir, options);
    if (status != CVSDK_OK)
      return status;
    *out_detector = detector.release();
    CVSDK_Log(CVSDK_LOG_INFO, "algo.detector", "detector created");
    return CVSDK_OK;
  } catch (const std::bad_alloc&) {
    cvsdk::SetLastError("allocation failed");
    return CVSDK_OUT_OF_MEMORY;
  } catch (const std::exception& error) {
    cvsdk::SetLastError(error.what());
    return CVSDK_INTERNAL_ERROR;
  } catch (...) {
    cvsdk::SetLastError("unknown internal exception");
    return CVSDK_INTERNAL_ERROR;
  }
}
CVSDK_Status CVSDK_DetectorInfer(CVSDK_Detector* detector, const CVSDK_Image* image,
                                 CVSDK_DetectionList* list) {
  try {
    if (!detector || !list || !HasSize(list->struct_size, sizeof(CVSDK_DetectionList))) {
      cvsdk::SetLastError("invalid detector infer arguments");
      return CVSDK_INVALID_ARGUMENT;
    }
    CVSDK_Status status = ValidateImage(image);
    if (status != CVSDK_OK) {
      CVSDK_Log(CVSDK_LOG_WARN, "api.detector", "invalid inference image");
      return status;
    }
    std::vector<cvsdk::Detection> result;
    status = detector->impl.Infer(*image, &result);
    if (status != CVSDK_OK)
      return status;
    list->count = static_cast<uint32_t>(result.size());
    if (!list->items)
      return result.empty() ? CVSDK_OK : CVSDK_BUFFER_TOO_SMALL;
    if (list->capacity < list->count) {
      cvsdk::SetLastError("detection buffer capacity is insufficient");
      return CVSDK_BUFFER_TOO_SMALL;
    }
    for (uint32_t i = 0; i < list->count; ++i)
      list->items[i] = {result[i].x,      result[i].y,     result[i].width,
                        result[i].height, result[i].score, result[i].class_id};
    return CVSDK_OK;
  } catch (const std::exception& error) {
    cvsdk::SetLastError(error.what());
    return CVSDK_INTERNAL_ERROR;
  } catch (...) {
    cvsdk::SetLastError("unknown internal exception");
    return CVSDK_INTERNAL_ERROR;
  }
}
void CVSDK_DetectorDestroy(CVSDK_Detector* detector) {
  delete detector;
}
CVSDK_Status CVSDK_FireFilterCreate(const char* path, CVSDK_FireFilter** out_filter) {
  try {
    if (!out_filter) {
      cvsdk::SetLastError("out_filter is required");
      return CVSDK_INVALID_ARGUMENT;
    }
    *out_filter = nullptr;
    cvsdk::FireConfig config;
    CVSDK_Status status = cvsdk::LoadFireConfig(path, &config);
    if (status != CVSDK_OK)
      return status;
    *out_filter = new CVSDK_FireFilter(config);
    CVSDK_Log(CVSDK_LOG_INFO, "algo.fire_filter", "fire filter created from JSON");
    return CVSDK_OK;
  } catch (const std::bad_alloc&) {
    cvsdk::SetLastError("allocation failed");
    return CVSDK_OUT_OF_MEMORY;
  } catch (const std::exception& error) {
    cvsdk::SetLastError(error.what());
    return CVSDK_INTERNAL_ERROR;
  } catch (...) {
    cvsdk::SetLastError("unknown internal exception");
    return CVSDK_INTERNAL_ERROR;
  }
}
CVSDK_Status CVSDK_FireFilterProcess(CVSDK_FireFilter* filter, uint32_t width, uint32_t height,
                                     const CVSDK_Detection* detections, uint32_t count,
                                     CVSDK_FireAlertState* state) {
  return filter ? filter->impl.Process(width, height, detections, count, state)
                : CVSDK_INVALID_ARGUMENT;
}
CVSDK_Status CVSDK_FireFilterReset(CVSDK_FireFilter* filter) {
  if (!filter) {
    cvsdk::SetLastError("fire filter is required");
    return CVSDK_INVALID_ARGUMENT;
  }
  filter->impl.Reset();
  return CVSDK_OK;
}
void CVSDK_FireFilterDestroy(CVSDK_FireFilter* filter) {
  delete filter;
}
CVSDK_Status CVSDK_FireSmokeProcessorCreate(const char* path,
                                            CVSDK_FireSmokeProcessor** out_processor) {
  try {
    if (!out_processor) {
      cvsdk::SetLastError("out_processor is required");
      return CVSDK_INVALID_ARGUMENT;
    }
    *out_processor = nullptr;
    cvsdk::FireConfig config;
    CVSDK_Status status = cvsdk::LoadFireConfig(path, &config);
    if (status != CVSDK_OK)
      return status;
    *out_processor = new CVSDK_FireSmokeProcessor(config);
    return CVSDK_OK;
  } catch (const std::bad_alloc&) {
    cvsdk::SetLastError("allocation failed");
    return CVSDK_OUT_OF_MEMORY;
  } catch (...) {
    cvsdk::SetLastError("processor create failed");
    return CVSDK_INTERNAL_ERROR;
  }
}
CVSDK_Status CVSDK_FireSmokeProcessorProcess(CVSDK_FireSmokeProcessor* processor,
                                             const CVSDK_Image* image, const CVSDK_Detection* raw,
                                             uint32_t count, CVSDK_DetectionList* list,
                                             CVSDK_FireAlertState* state) {
  if (!processor || !image || !list || list->struct_size < sizeof(CVSDK_DetectionList)) {
    cvsdk::SetLastError("invalid fire/smoke processor arguments");
    return CVSDK_INVALID_ARGUMENT;
  }
  std::vector<CVSDK_Detection> output;
  CVSDK_Status status = processor->impl.Process(*image, raw, count, &output, state);
  if (status != CVSDK_OK)
    return status;
  list->count = static_cast<uint32_t>(output.size());
  if (!list->items)
    return output.empty() ? CVSDK_OK : CVSDK_BUFFER_TOO_SMALL;
  if (list->capacity < list->count) {
    cvsdk::SetLastError("filtered detection buffer capacity is insufficient");
    return CVSDK_BUFFER_TOO_SMALL;
  }
  for (uint32_t i = 0; i < list->count; ++i)
    list->items[i] = output[i];
  return CVSDK_OK;
}
CVSDK_Status CVSDK_FireSmokeProcessorReset(CVSDK_FireSmokeProcessor* processor) {
  if (!processor) {
    cvsdk::SetLastError("processor is required");
    return CVSDK_INVALID_ARGUMENT;
  }
  processor->impl.Reset();
  return CVSDK_OK;
}
void CVSDK_FireSmokeProcessorDestroy(CVSDK_FireSmokeProcessor* processor) {
  delete processor;
}
}
