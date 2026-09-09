#ifndef CV_SDK_CV_SDK_H_
#define CV_SDK_CV_SDK_H_

#include <stdint.h>

#if defined(_WIN32)
#if defined(CVSDK_BUILDING_LIBRARY)
#define CVSDK_API __declspec(dllexport)
#else
#define CVSDK_API __declspec(dllimport)
#endif
#else
#define CVSDK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CVSDK_API_VERSION 2u

typedef struct CVSDK_Detector CVSDK_Detector;
typedef struct CVSDK_FireFilter CVSDK_FireFilter;
typedef struct CVSDK_FireSmokeProcessor CVSDK_FireSmokeProcessor;

typedef enum CVSDK_Status {
  CVSDK_OK = 0,
  CVSDK_INVALID_ARGUMENT = 1,
  CVSDK_NOT_FOUND = 2,
  CVSDK_UNSUPPORTED = 3,
  CVSDK_OUT_OF_MEMORY = 4,
  CVSDK_INTERNAL_ERROR = 5,
  CVSDK_BUFFER_TOO_SMALL = 6
} CVSDK_Status;

typedef enum CVSDK_PixelFormat {
  CVSDK_PIXEL_FORMAT_BGR8 = 1,
  CVSDK_PIXEL_FORMAT_RGB8 = 2,
  CVSDK_PIXEL_FORMAT_GRAY8 = 3
} CVSDK_PixelFormat;

typedef enum CVSDK_LogLevel {
  CVSDK_LOG_TRACE = 0,
  CVSDK_LOG_DEBUG = 1,
  CVSDK_LOG_INFO = 2,
  CVSDK_LOG_WARN = 3,
  CVSDK_LOG_ERROR = 4,
  CVSDK_LOG_FATAL = 5,
  CVSDK_LOG_OFF = 6
} CVSDK_LogLevel;

/* Invoked by the SDK logging worker thread. The JSON string is valid only during this call. */
typedef void (*CVSDK_LogCallback)(CVSDK_LogLevel level, const char* message_json, void* user_data);

typedef struct CVSDK_LogOptions {
  uint32_t struct_size;
  CVSDK_LogLevel min_level;   /* defaults to WARN when options is NULL */
  const char* file_path;      /* NULL disables file output */
  uint64_t max_file_bytes;    /* 0 disables rolling; recommended: 20 MiB */
  uint32_t max_rotated_files; /* used only when max_file_bytes > 0 */
  uint32_t queue_capacity;    /* 0 selects 4096 */
  CVSDK_LogCallback callback; /* optional business-owned sink */
  void* user_data;
} CVSDK_LogOptions;

typedef struct CVSDK_LogStats {
  uint32_t struct_size;
  uint64_t accepted_count;
  uint64_t dropped_count;
  uint32_t queued_count;
} CVSDK_LogStats;

/* Caller owns data and must keep it valid for the duration of this synchronous call. */
typedef struct CVSDK_Image {
  uint32_t struct_size;
  const uint8_t* data;
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes;
  CVSDK_PixelFormat pixel_format;
} CVSDK_Image;

typedef struct CVSDK_Detection {
  float x;
  float y;
  float width;
  float height;
  float score;
  int32_t class_id;
} CVSDK_Detection;

/* Set items to NULL to query the capacity needed. The caller owns items storage. */
typedef struct CVSDK_DetectionList {
  uint32_t struct_size;
  CVSDK_Detection* items;
  uint32_t capacity;
  uint32_t count;
} CVSDK_DetectionList;

typedef struct CVSDK_DetectorOptions {
  uint32_t struct_size;
  const char* backend;   /* MVP supports only "mock". NULL selects it. */
  float score_threshold; /* [0, 1], default 0.25 */
  uint32_t reserved[8];
} CVSDK_DetectorOptions;

typedef enum CVSDK_FireAlertLevel {
  CVSDK_FIRE_ALERT_NONE = 0,
  CVSDK_FIRE_ALERT_INFO = 1,
  CVSDK_FIRE_ALERT_WARNING_SMOKE = 2,
  CVSDK_FIRE_ALERT_WARNING_FIRE = 3,
  CVSDK_FIRE_ALERT_CRITICAL = 4
} CVSDK_FireAlertLevel;

typedef struct CVSDK_FireAlertState {
  uint32_t struct_size;
  CVSDK_FireAlertLevel level;
  float max_fire_confidence;
  float max_smoke_confidence;
  uint32_t fire_hits;
  uint32_t smoke_hits;
  char reason[96];
} CVSDK_FireAlertState;

CVSDK_API uint32_t CVSDK_GetApiVersion(void);
CVSDK_API const char* CVSDK_StatusMessage(CVSDK_Status status);
CVSDK_API const char* CVSDK_GetLastError(void);
/* Reconfiguration synchronously replaces output sinks. Call before creating worker threads if
 * possible. */
CVSDK_API CVSDK_Status CVSDK_ConfigureLogging(const CVSDK_LogOptions* options);
CVSDK_API CVSDK_Status CVSDK_GetLogStats(CVSDK_LogStats* out_stats);
/* General SDK log entry point. Message must not contain sensitive image/OCR data. */
CVSDK_API void CVSDK_Log(CVSDK_LogLevel level, const char* module, const char* message);

/* model_package must point to a directory containing manifest.json in this MVP. */
CVSDK_API CVSDK_Status CVSDK_DetectorCreate(const char* model_package,
                                            const CVSDK_DetectorOptions* options,
                                            CVSDK_Detector** out_detector);
CVSDK_API CVSDK_Status CVSDK_DetectorInfer(CVSDK_Detector* detector, const CVSDK_Image* image,
                                           CVSDK_DetectionList* in_out_detections);
CVSDK_API void CVSDK_DetectorDestroy(CVSDK_Detector* detector);

/* Loads and validates a JSON file. Each instance must be used by one camera stream only. */
CVSDK_API CVSDK_Status CVSDK_FireFilterCreate(const char* config_json_path,
                                              CVSDK_FireFilter** out_filter);
/* Detections use class_id 0=smoke and 1=fire. x/y/width/height are original-image pixels. */
CVSDK_API CVSDK_Status CVSDK_FireFilterProcess(CVSDK_FireFilter* filter, uint32_t image_width,
                                               uint32_t image_height,
                                               const CVSDK_Detection* detections,
                                               uint32_t detection_count,
                                               CVSDK_FireAlertState* out_state);
CVSDK_API CVSDK_Status CVSDK_FireFilterReset(CVSDK_FireFilter* filter);
CVSDK_API void CVSDK_FireFilterDestroy(CVSDK_FireFilter* filter);

/* Fire/smoke postprocess pipeline: color gate -> area/temporal fire filter.
 * Raw class IDs must be 0=smoke and 1=fire. */
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorCreate(const char* config_json_path,
                                                      CVSDK_FireSmokeProcessor** out_processor);
/* `filtered_detections` uses the same caller-allocated capacity/query contract as DetectorInfer. */
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorProcess(CVSDK_FireSmokeProcessor* processor,
                                                       const CVSDK_Image* image,
                                                       const CVSDK_Detection* raw_detections,
                                                       uint32_t raw_detection_count,
                                                       CVSDK_DetectionList* filtered_detections,
                                                       CVSDK_FireAlertState* out_state);
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorReset(CVSDK_FireSmokeProcessor* processor);
CVSDK_API void CVSDK_FireSmokeProcessorDestroy(CVSDK_FireSmokeProcessor* processor);

#ifdef __cplusplus
}
#endif
#endif
