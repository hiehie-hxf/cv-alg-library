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
/** 仪表读数器句柄：加载仪表检测和姿态模型后可同步读取单帧中的多个指针仪表。 */
typedef struct CVSDK_GaugeReader CVSDK_GaugeReader;
/** 七段数码管仪表读数器句柄；不依赖推理模型。 */
typedef struct CVSDK_DigitalGaugeReader CVSDK_DigitalGaugeReader;

typedef enum CVSDK_Status {
  CVSDK_OK = 0,               /* 调用成功 */
  CVSDK_INVALID_ARGUMENT = 1, /* 参数为空、结构体大小或字段值不合法 */
  CVSDK_NOT_FOUND = 2,        /* 模型、配置等指定资源不存在 */
  CVSDK_UNSUPPORTED = 3,      /* 请求的后端、格式或能力未被当前构建支持 */
  CVSDK_OUT_OF_MEMORY = 4,    /* 内存分配失败 */
  CVSDK_INTERNAL_ERROR = 5,   /* 推理引擎或 SDK 内部错误 */
  CVSDK_BUFFER_TOO_SMALL = 6  /* 调用方提供的输出数组容量不足 */
} CVSDK_Status;

typedef enum CVSDK_PixelFormat {
  CVSDK_PIXEL_FORMAT_BGR8 = 1, /* 每像素 3 字节，B、G、R 顺序 */
  CVSDK_PIXEL_FORMAT_RGB8 = 2, /* 每像素 3 字节，R、G、B 顺序 */
  CVSDK_PIXEL_FORMAT_GRAY8 = 3 /* 每像素 1 字节，灰度图 */
} CVSDK_PixelFormat;

typedef enum CVSDK_LogLevel {
  CVSDK_LOG_TRACE = 0, /* 最细粒度的跟踪日志 */
  CVSDK_LOG_DEBUG = 1, /* 调试日志 */
  CVSDK_LOG_INFO = 2,  /* 常规运行信息 */
  CVSDK_LOG_WARN = 3,  /* 可恢复的异常或风险提示 */
  CVSDK_LOG_ERROR = 4, /* 调用或运行失败 */
  CVSDK_LOG_FATAL = 5, /* 严重错误，服务通常无法继续正常工作 */
  CVSDK_LOG_OFF = 6    /* 禁用全部日志输出 */
} CVSDK_LogLevel;

/* Invoked by the SDK logging worker thread. The JSON string is valid only during this call. */
typedef void (*CVSDK_LogCallback)(CVSDK_LogLevel level, const char* message_json, void* user_data);

typedef struct CVSDK_LogOptions {
  uint32_t struct_size;       /* 结构体大小，必须设置为 sizeof(CVSDK_LogOptions) */
  CVSDK_LogLevel min_level;   /* 最低日志等级；options=NULL 时默认为 WARN */
  const char* file_path;      /* 日志文件路径；NULL 表示不写文件 */
  uint64_t max_file_bytes;    /* 单个日志文件最大字节数；0 表示不滚动 */
  uint32_t max_rotated_files; /* 保留的历史文件数量；仅 max_file_bytes>0 时生效 */
  uint32_t queue_capacity;    /* 异步队列容量；0 表示使用默认值 4096 */
  CVSDK_LogCallback callback; /* 业务日志回调；可选 */
  void* user_data;            /* 原样传递给 callback 的用户数据 */
} CVSDK_LogOptions;

typedef struct CVSDK_LogStats {
  uint32_t struct_size;    /* 结构体大小，必须设置为 sizeof(CVSDK_LogStats) */
  uint64_t accepted_count; /* 已接受的日志条数 */
  uint64_t dropped_count;  /* 因队列满等原因丢弃的日志条数 */
  uint32_t queued_count;   /* 当前队列中的日志条数 */
} CVSDK_LogStats;

/**
 * 输入图像描述。
 * data 由调用方拥有，仅需在同步 API 返回前保持有效；SDK 不会异步持有该指针。
 * stride_bytes 允许图像行尾存在对齐填充，必须不小于实际像素行宽。
 */
typedef struct CVSDK_Image {
  uint32_t struct_size;           /* 结构体大小，必须设置为 sizeof(CVSDK_Image) */
  const uint8_t* data;            /* 图像数据；由调用方分配并在同步调用期间保持有效 */
  uint32_t width;                 /* 图像宽度，单位为像素 */
  uint32_t height;                /* 图像高度，单位为像素 */
  uint32_t stride_bytes;          /* 每行字节数，允许包含行尾对齐填充 */
  CVSDK_PixelFormat pixel_format; /* 像素格式：BGR8、RGB8 或 GRAY8 */
} CVSDK_Image;

typedef struct CVSDK_Detection {
  float x;          /* 检测框左上角 X 坐标，原图像素 */
  float y;          /* 检测框左上角 Y 坐标，原图像素 */
  float width;      /* 检测框宽度，原图像素 */
  float height;     /* 检测框高度，原图像素 */
  float score;      /* 置信度，范围通常为 [0, 1] */
  int32_t class_id; /* 类别 ID；火情模型约定 0=smoke、1=fire */
} CVSDK_Detection;

/**
 * 检测结果数组描述。
 * items=NULL 时用于查询所需容量；容量不足返回 CVSDK_BUFFER_TOO_SMALL，count 写入所需数量。
 * items 的内存始终由调用方分配和释放，SDK 不负责释放。
 */
typedef struct CVSDK_DetectionList {
  uint32_t struct_size;   /* 结构体大小，必须设置为 sizeof(CVSDK_DetectionList) */
  CVSDK_Detection* items; /* 调用方分配的检测结果数组；NULL 可用于查询容量 */
  uint32_t capacity;      /* items 数组可容纳的元素数量 */
  uint32_t count;         /* 输出：实际检测数量或所需容量 */
} CVSDK_DetectionList;

/** 检测器运行参数；模型输入尺寸、类别顺序等模型契约由 manifest 管理。 */
typedef struct CVSDK_DetectorOptions {
  uint32_t struct_size;  /* 结构体大小，必须设置为 sizeof(CVSDK_DetectorOptions) */
  const char* backend;   /* 推理后端：mock、onnxruntime 或 onnxruntime-cuda；NULL 为 mock */
  float score_threshold; /* 结果置信度阈值，范围 [0, 1]，默认值为 0.25 */
  uint32_t reserved[8];  /* 预留字段，必须初始化为 0 */
} CVSDK_DetectorOptions;

typedef enum CVSDK_FireAlertLevel {
  CVSDK_FIRE_ALERT_NONE = 0,          /* 未触发告警 */
  CVSDK_FIRE_ALERT_INFO = 1,          /* 低风险提示 */
  CVSDK_FIRE_ALERT_WARNING_SMOKE = 2, /* 烟雾告警 */
  CVSDK_FIRE_ALERT_WARNING_FIRE = 3,  /* 火焰告警 */
  CVSDK_FIRE_ALERT_CRITICAL = 4       /* 严重火情告警 */
} CVSDK_FireAlertLevel;

typedef struct CVSDK_FireAlertState {
  uint32_t struct_size;       /* 结构体大小，必须设置为 sizeof(CVSDK_FireAlertState) */
  CVSDK_FireAlertLevel level; /* 当前告警等级 */
  float max_fire_confidence;  /* 当前窗口内最高火焰置信度 */
  float max_smoke_confidence; /* 当前窗口内最高烟雾置信度 */
  uint32_t fire_hits;         /* 当前窗口内火焰命中次数 */
  uint32_t smoke_hits;        /* 当前窗口内烟雾命中次数 */
  char reason[96];            /* 可读告警原因，UTF-8，以 NUL 结尾 */
} CVSDK_FireAlertState;

typedef struct CVSDK_GaugeReaderOptions {
  uint32_t struct_size;       /* 结构体大小，必须设置为 sizeof(CVSDK_GaugeReaderOptions) */
  const char* backend;        /* 推理后端：onnxruntime 或 onnxruntime-cuda */
  float detection_threshold;  /* 仪表检测框置信度阈值，范围 [0, 1] */
  float keypoint_threshold;   /* 关键点与姿态目标置信度阈值，范围 [0, 1] */
  float range_min;            /* 仪表量程最小值，必须小于 range_max */
  float range_max;            /* 仪表量程最大值 */
  const char* unit;           /* 量程单位，例如 MPa；NULL 表示空单位 */
  uint32_t apply_calibration; /* 非 0 使用原算法的读数校准偏移 */
  uint32_t clamp_to_range;    /* 非 0 将读数限制在 [range_min, range_max] */
  uint32_t reserved[6];       /* 预留字段，必须初始化为 0 */
} CVSDK_GaugeReaderOptions;

typedef struct CVSDK_GaugeReading {
  float x;               /* 仪表检测框左上角 X 坐标，原图像素 */
  float y;               /* 仪表检测框左上角 Y 坐标，原图像素 */
  float width;           /* 仪表检测框宽度，原图像素 */
  float height;          /* 仪表检测框高度，原图像素 */
  float detection_score; /* 仪表检测框置信度 */
  float pose_score;      /* 三类关键点中最低的姿态置信度；失败时为 0 */
  float ratio;           /* 指针在起止刻度圆弧上的比例 */
  float value;           /* 最终换算后的仪表读数 */
  int32_t status;        /* 0=成功，1=缺少有效姿态关键点 */
  char unit[16];         /* 创建参数中的单位，UTF-8，以 NUL 结尾 */
} CVSDK_GaugeReading;

typedef enum CVSDK_DigitalGaugeRowRole {
  CVSDK_DIGITAL_GAUGE_ROW_UNKNOWN = 0, /* 未知行角色 */
  CVSDK_DIGITAL_GAUGE_ROW_PV = 1,      /* 过程值行 */
  CVSDK_DIGITAL_GAUGE_ROW_SV = 2       /* 设定值行 */
} CVSDK_DigitalGaugeRowRole;

typedef enum CVSDK_DigitalGaugeFlags {
  CVSDK_DIGITAL_GAUGE_FLAG_NONE = 0,                 /* 无告警 */
  CVSDK_DIGITAL_GAUGE_FLAG_MISSING_DIGIT = 1u << 0,  /* 相对基准阈值存在漏检 */
  CVSDK_DIGITAL_GAUGE_FLAG_MULTIPLE_DOTS = 1u << 1,  /* 检出多个小数点 */
  CVSDK_DIGITAL_GAUGE_FLAG_LOW_CONFIDENCE = 1u << 2, /* 段位判据接近阈值 */
  CVSDK_DIGITAL_GAUGE_FLAG_CODE_CORRECTED = 1u << 3, /* 使用汉明距离纠正段码 */
  CVSDK_DIGITAL_GAUGE_FLAG_LOW_SPECIFICITY = 1u << 4 /* 段间暗缝特异性不足 */
} CVSDK_DigitalGaugeFlags;

typedef struct CVSDK_DigitalGaugeReaderOptions {
  uint32_t struct_size;      /* 必须设置为 sizeof(CVSDK_DigitalGaugeReaderOptions) */
  float minimum_confidence;  /* [0,1]；低于阈值的行仅在 return_ambiguous!=0 时返回 */
  uint32_t return_ambiguous; /* 非 0 返回带歧义标志的读数 */
  uint32_t reserved[8];      /* 预留字段，必须初始化为 0 */
} CVSDK_DigitalGaugeReaderOptions;

typedef struct CVSDK_DigitalGaugeReading {
  uint32_t struct_size;                              /* 输出结构体版本大小 */
  uint32_t panel_index;                              /* 面板序号，按原图 X 坐标从左向右 */
  CVSDK_DigitalGaugeRowRole role;                    /* PV 或 SV */
  float panel_x, panel_y, panel_width, panel_height; /* 面板框，原图像素 */
  float row_x, row_y, row_width, row_height;         /* 行带框，原图像素 */
  char text[16];                                     /* 解码文本，以 NUL 结尾 */
  double value;                                      /* 数值结果；仅 has_value!=0 时有效 */
  uint32_t has_value;                                /* 文本可完整转换为数值时为非 0 */
  float confidence;                                  /* 行级置信度，范围 [0,1] */
  float segment_specificity;                         /* 段与邻域暗缝的可分性，范围 [0,1] */
  float shear;                                       /* 估计的字符斜切量 */
  float pitch;                                       /* 相邻数字 cell 间距，像素 */
  uint32_t digit_count;                              /* 识别出的数字位数 */
  uint32_t flags;                                    /* CVSDK_DigitalGaugeFlags 按位组合 */
} CVSDK_DigitalGaugeReading;

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

/** 创建仪表读数器。模型包须包含 artifacts/onnxruntime/detector.onnx 和 pose.onnx。 */
CVSDK_API CVSDK_Status CVSDK_GaugeReaderCreate(const char* model_package,
                                               const CVSDK_GaugeReaderOptions* options,
                                               CVSDK_GaugeReader** out_reader);
/** 同步读取一帧中的指针仪表；items=NULL 时查询所需输出容量。 */
CVSDK_API CVSDK_Status CVSDK_GaugeReaderInfer(CVSDK_GaugeReader* reader, const CVSDK_Image* image,
                                              CVSDK_GaugeReading* items, uint32_t capacity,
                                              uint32_t* out_count);
/** 销毁仪表读数器。 */
CVSDK_API void CVSDK_GaugeReaderDestroy(CVSDK_GaugeReader* reader);

/** 创建纯 OpenCV 七段数码管仪表读数器。 */
CVSDK_API CVSDK_Status CVSDK_DigitalGaugeReaderCreate(
    const char* package_dir, const CVSDK_DigitalGaugeReaderOptions* options,
    CVSDK_DigitalGaugeReader** out_reader);
/** 同步读取数字仪表的 PV/SV 行；items=NULL 时查询容量。 */
CVSDK_API CVSDK_Status CVSDK_DigitalGaugeReaderInfer(CVSDK_DigitalGaugeReader* reader,
                                                     const CVSDK_Image* image,
                                                     CVSDK_DigitalGaugeReading* items,
                                                     uint32_t capacity, uint32_t* out_count);
/** 销毁数字仪表读数器。 */
CVSDK_API void CVSDK_DigitalGaugeReaderDestroy(CVSDK_DigitalGaugeReader* reader);

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
