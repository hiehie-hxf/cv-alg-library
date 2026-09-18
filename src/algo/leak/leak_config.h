#pragma once

#include "cv_sdk/cv_sdk.h"

namespace cvsdk {
/** 漏液算法运行参数。字段名称与 JSON schema 一一对应，加载后不再依赖字符串查询。 */
struct LeakConfig {
  float min_area_ratio = .0005F;
  float candidate_conf = .25F;
  float iou_threshold = .50F;
  float mask_threshold = .50F;
  uint32_t window = 10, min_hits = 3;
  float area_growth_ratio = 1.15F;
  float centroid_down_px = 5.0F;
};
/** 输入：JSON 文件路径；输出：填充强类型配置并返回校验状态。 */
CVSDK_Status LoadLeakConfig(const char* path, LeakConfig* config);
} // namespace cvsdk
