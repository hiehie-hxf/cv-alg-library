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

/** 检测器句柄：由 CVSDK_DetectorCreate 创建，使用结束后必须调用 Destroy 释放。 */
typedef struct CVSDK_Detector CVSDK_Detector;
/** 火情时序过滤器句柄；每路视频流应独立创建一个实例。 */
typedef struct CVSDK_FireFilter CVSDK_FireFilter;
/** 火焰/烟雾完整后处理句柄；内部包含颜色门控、烟雾门控和时序状态。 */
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

/**
 * 输入图像描述。
 * data 由调用方拥有，仅需在同步 API 返回前保持有效；SDK 不会异步持有该指针。
 * stride_bytes 允许图像行尾存在对齐填充，必须不小于实际像素行宽。
 */
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

/**
 * 检测结果数组描述。
 * items=NULL 时用于查询所需容量；容量不足返回 CVSDK_BUFFER_TOO_SMALL，count 写入所需数量。
 * items 的内存始终由调用方分配和释放，SDK 不负责释放。
 */
typedef struct CVSDK_DetectionList {
  uint32_t struct_size;
  CVSDK_Detection* items;
  uint32_t capacity;
  uint32_t count;
} CVSDK_DetectionList;

/** 检测器运行参数；模型输入尺寸、类别顺序等模型契约由 manifest 管理。 */
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

/**
 * @brief 获取当前 C ABI 版本号
 * @return 当前 API 版本号
 * @note 返回值用于调用方与 SDK 进行 ABI 兼容性校验。
 */
CVSDK_API uint32_t CVSDK_GetApiVersion(void);
/**
 * @brief 将状态码转换为可读描述
 * @param status 待查询的 SDK 状态码
 * @return 静态状态描述字符串，不得由调用方释放；未知值返回 unknown status
 */
CVSDK_API const char* CVSDK_StatusMessage(CVSDK_Status status);
/**
 * @brief 获取当前线程最近一次错误信息
 * @return 错误描述字符串，不得由调用方释放；下一次 SDK 调用可能覆盖该内容
 * @note 错误信息使用线程本地存储，并非全局共享字符串。
 */
CVSDK_API const char* CVSDK_GetLastError(void);
/* Reconfiguration synchronously replaces output sinks. Call before creating worker threads if
 * possible. */
/**
 * @brief 配置 SDK 日志系统
 * @param options 日志配置；传入 NULL 恢复默认配置
 * @return CVSDK_OK 表示配置成功，否则返回错误码
 * @note 配置会同步替换日志输出目标；业务回调运行在 SDK 日志线程。
 */
CVSDK_API CVSDK_Status CVSDK_ConfigureLogging(const CVSDK_LogOptions* options);
/**
 * @brief 获取异步日志统计信息
 * @param out_stats 输出日志接收数、丢弃数和当前队列长度
 * @return CVSDK_OK 表示获取成功，否则返回错误码
 */
CVSDK_API CVSDK_Status CVSDK_GetLogStats(CVSDK_LogStats* out_stats);
/**
 * @brief 写入一条 SDK 日志
 * @param level 日志等级
 * @param module 模块名称，不能为空
 * @param message 日志正文，不能为空且不得包含敏感图像/OCR 数据
 * @return 无返回值；非法参数或队列满时日志可能被忽略/丢弃
 */
CVSDK_API void CVSDK_Log(CVSDK_LogLevel level, const char* module, const char* message);

/**
 * @brief 创建并加载目标检测器
 * @param model_package 模型包目录，必须包含 manifest.json 和后端 artifact
 * @param options 检测器运行参数，可传 NULL 使用默认值
 * @param out_detector 输出新建的检测器句柄
 * @return CVSDK_OK 表示创建成功，否则返回错误码
 * @note 成功返回的句柄必须通过 CVSDK_DetectorDestroy 释放。
 */
CVSDK_API CVSDK_Status CVSDK_DetectorCreate(const char* model_package,
                                            const CVSDK_DetectorOptions* options,
                                            CVSDK_Detector** out_detector);
/**
 * @brief 执行一次同步目标检测
 * @param detector 已创建的检测器句柄
 * @param image 输入图像；数据由调用方持有，调用期间必须保持有效
 * @param in_out_detections 输出检测框数组；items=NULL 可查询所需容量
 * @return CVSDK_OK 表示成功；容量不足返回 CVSDK_BUFFER_TOO_SMALL
 * @note 函数不会异步保存 image->data 指针。
 */
CVSDK_API CVSDK_Status CVSDK_DetectorInfer(CVSDK_Detector* detector, const CVSDK_Image* image,
                                           CVSDK_DetectionList* in_out_detections);
/**
 * @brief 销毁目标检测器
 * @param detector 待销毁的检测器句柄，可传 NULL
 * @return 无返回值；调用后句柄不可继续使用
 */
CVSDK_API void CVSDK_DetectorDestroy(CVSDK_Detector* detector);

/**
 * @brief 创建火焰/烟雾时序过滤器
 * @param config_json_path 火情规则 JSON 文件路径
 * @param out_filter 输出新建的过滤器句柄
 * @return CVSDK_OK 表示加载并校验成功，否则返回错误码
 * @note 每路视频流必须使用独立实例。
 */
CVSDK_API CVSDK_Status CVSDK_FireFilterCreate(const char* config_json_path,
                                              CVSDK_FireFilter** out_filter);
/**
 * @brief 处理单帧火焰/烟雾检测结果
 * @param filter 火情过滤器句柄
 * @param image_width 输入图像宽度（像素）
 * @param image_height 输入图像高度（像素）
 * @param detections 输入检测框数组，class_id 固定为 0=smoke、1=fire
 * @param detection_count 输入检测框数量；为 0 时 detections 可为 NULL
 * @param out_state 输出当前告警等级、窗口命中数和最高置信度
 * @return CVSDK_OK 表示处理成功，否则返回错误码
 * @note 检测框坐标使用原图像素；函数会更新跨帧滑动窗口。
 */
CVSDK_API CVSDK_Status CVSDK_FireFilterProcess(CVSDK_FireFilter* filter, uint32_t image_width,
                                               uint32_t image_height,
                                               const CVSDK_Detection* detections,
                                               uint32_t detection_count,
                                               CVSDK_FireAlertState* out_state);
/**
 * @brief 重置火焰/烟雾时序状态
 * @param filter 待重置的过滤器句柄
 * @return CVSDK_OK 表示成功，否则返回错误码
 */
CVSDK_API CVSDK_Status CVSDK_FireFilterReset(CVSDK_FireFilter* filter);
/**
 * @brief 销毁火焰/烟雾过滤器
 * @param filter 待销毁的过滤器句柄，可传 NULL
 * @return 无返回值
 */
CVSDK_API void CVSDK_FireFilterDestroy(CVSDK_FireFilter* filter);

/**
 * @brief 创建完整火焰/烟雾后处理器
 * @param config_json_path 火情规则 JSON 文件路径
 * @param out_processor 输出新建的处理器句柄
 * @return CVSDK_OK 表示成功，否则返回错误码
 * @note 处理器包含颜色门控、烟雾时空门控和多帧告警状态。
 */
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorCreate(const char* config_json_path,
                                                      CVSDK_FireSmokeProcessor** out_processor);
/**
 * @brief 执行完整火焰/烟雾后处理
 * @param processor 火情处理器句柄
 * @param image 当前 BGR/RGB 输入图像
 * @param raw_detections 模型原始检测框数组
 * @param raw_detection_count 原始检测框数量
 * @param filtered_detections 输出过滤后的检测框，遵循 DetectorInfer 的容量查询协议
 * @param out_state 输出当前火情告警状态
 * @return CVSDK_OK 表示成功；输出容量不足返回 CVSDK_BUFFER_TOO_SMALL
 * @note class_id 必须为 0=smoke、1=fire；处理器会更新跨帧状态。
 */
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorProcess(CVSDK_FireSmokeProcessor* processor,
                                                       const CVSDK_Image* image,
                                                       const CVSDK_Detection* raw_detections,
                                                       uint32_t raw_detection_count,
                                                       CVSDK_DetectionList* filtered_detections,
                                                       CVSDK_FireAlertState* out_state);
/** 输入：处理器句柄。输出：清空颜色/烟雾/时序状态并返回状态码。 */
CVSDK_API CVSDK_Status CVSDK_FireSmokeProcessorReset(CVSDK_FireSmokeProcessor* processor);
/** 输入：处理器句柄；输出：释放全部处理资源。 */
CVSDK_API void CVSDK_FireSmokeProcessorDestroy(CVSDK_FireSmokeProcessor* processor);

#ifdef __cplusplus
}
#endif
#endif
